/*
 *    Copyright (C) 2019-2021 Joshua Boudreau <jboudreau@45drives.com>
 *    
 *    This file is part of autotier.
 * 
 *    autotier is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 * 
 *    autotier is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 * 
 *    You should have received a copy of the GNU General Public License
 *    along with autotier.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "config.hpp"
#include "alert.hpp"
#include "tier.hpp"
#include <sstream>
#include <regex>

inline void strip_whitespace(std::string &str){
	std::size_t strItr;
	// back ws
	if((strItr = str.find('#')) == std::string::npos){ // strItr point to '#' or end
		strItr = str.length();
	}
	if(strItr != 0) // protect from underflow
		strItr--; // point to last character
	while(strItr && (str.at(strItr) == ' ' || str.at(strItr) == '\t')){ // remove whitespace
		strItr--;
	} // strItr points to last character
	str = str.substr(0,strItr + 1);
	// front ws
	strItr = 0;
	while(strItr < str.length() && (str.at(strItr) == ' ' || str.at(strItr) == '\t')){ // remove whitespace
		strItr++;
	} // strItr points to first character
	str = str.substr(strItr, str.length() - strItr);
}

Config::Config(const fs::path &config_path, std::list<Tier> &tiers, const ConfigOverrides &config_overrides){
	std::string line, key, value;
	
	// open file
	std::ifstream config_file(config_path.string());
	if(!config_file){
		config_file.close();
		init_config_file(config_path);
		config_file.open(config_path.string());
	}
	
	Tier *tptr = nullptr;
	while(config_file){
		getline(config_file, line);
		
		strip_whitespace(line);
		// full line comments:
		if(line.empty() || line.front() == '#')
			continue; // ignore comments
		
		if(line.front() == '['){
			std::string id = line.substr(1,line.find(']')-1);
			if(regex_match(id,std::regex("^\\s*[Gg]lobal\\s*$"))){
				if(load_global(config_file, id) == EOF) break;
			}
			Logging::log.message("ID: \"" + id + "\"", 2);
			tiers.emplace_back(id);
			tptr = &tiers.back();
		}else if(tptr){
			std::stringstream line_stream(line);
			
			getline(line_stream, key, '=');
			getline(line_stream, value);
			
			strip_whitespace(key);
			strip_whitespace(value);
			
			if(key.empty() || value.empty())
				continue; // ignore unassigned fields
			
			if(key == "Path"){
				Logging::log.message("Found Path: \"" + value + "\"", 2);
				tptr->path(value);
			}else if(key == "Watermark"){
				Logging::log.message("Found Watermark: \"" + value + "\"", 2);
				try{
					tptr->watermark(stoi(value));
				}catch(const std::invalid_argument &){
					tptr->watermark(-1);
				}
			} // else ignore
		}
	}
	
	if(config_overrides.log_level_override.overridden()){
		log_level_ = config_overrides.log_level_override.value();
	}
	
	verify(config_path, tiers);
	
	Logging::log.set_level(log_level_);
	
	for(Tier & t: tiers){
		t.calc_watermark_bytes();
		t.get_capacity_and_usage();
	}
}

int Config::load_global(std::ifstream &config_file, std::string &id){
	std::string line, key, value;
	while(config_file){
		getline(config_file, line);
		
		strip_whitespace(line);
		// full line comments:
		if(line.empty() || line.front() == '#')
			continue; // ignore comments
		
		if(line.front() == '['){
			id = line.substr(1,line.find(']')-1);
			return 0;
		}
		
		std::stringstream line_stream(line);
		getline(line_stream, key, '=');
		getline(line_stream, value);
		strip_whitespace(key);
		strip_whitespace(value);
		
		if(key == "Log Level"){
			try{
				log_level_ = stoi(value);
			}catch(const std::invalid_argument &){
				log_level_ = -1;
			}
		}else if(key == "Tier Period"){
			try{
				tier_period_s_ = std::chrono::seconds(stoi(value));
			}catch(const std::invalid_argument &){
				tier_period_s_ = std::chrono::seconds(-1);
			}
		} // else if ...
	}
	// if here, EOF reached
	return EOF;
}

void Config::init_config_file(const fs::path &config_path) const{
	boost::system::error_code ec;
	fs::create_directories(config_path.parent_path(), ec);
	if(ec) Logging::log.error("Error creating path: " + config_path.parent_path().string());
	std::ofstream f(config_path.string());
	if(!f) Logging::log.error("Error opening config file: " + config_path.string());
	f <<
	"# autotier config\n"
	"[Global]                       # global settings\n"
	"Log Level = 1                  # 0 = none, 1 = normal, 2 = debug\n"
	"Tier Period = 1000             # number of seconds between file move batches\n"
	"\n"
	"[Tier 1]                       # tier name\n"
	"Path =                         # full path to tier storage pool\n"
	"Watermark =                    # % usage at which to stop filling tier\n"
	"\n"
	"[Tier 2]\n"
	"Path =\n"
	"Watermark =\n"
	"# ... (add as many tiers as you like)\n";
	f.close();
}

void Config::verify(const fs::path &config_path, const std::list<Tier> &tiers) const{
	bool errors = false;
	if(tiers.empty()){
		Logging::log.error("No tiers defined.", false);
		errors = true;
	}else if(tiers.size() == 1){
		Logging::log.error("Only one tier is defined. Two or more are needed.", false);
		errors = true;
	}else{
		for(const Tier &tier : tiers){
			if(!fs::is_directory(tier.path())){
				Logging::log.error(tier.id() + ": Not a directory: " + tier.path().string(), false);
				errors = true;
			}
			if(tier.watermark() > 100 || tier.watermark() < 0){
				Logging::log.error(tier.id() + ": Invalid watermark: " + std::to_string(tier.watermark()), false);
				errors = true;
			}
		}
	}
	if(log_level_ == -1){
		Logging::log.error("Invalid log level. (Log Level)", false);
		errors = true;
	}
	if(tier_period_s_ == std::chrono::seconds(-1)){
		Logging::log.error("Invalid tier period. (Tier Period)", false);
		errors = true;
	}
	if(errors){
		Logging::log.error("Please fix these mistakes in " + config_path.string());
	}
}

std::chrono::seconds Config::tier_period_s(void) const{
	return tier_period_s_;
}

void Config::dump(const std::list<Tier> &tiers) const{
	Logging::log.message("[Global]", 1);
	Logging::log.message("Log Level = " + std::to_string(log_level_), 1);
	Logging::log.message("Tier Period = " + std::to_string(tier_period_s_.count()), 1);
	Logging::log.message("", 1);
	for(const Tier &t : tiers){
		Logging::log.message("[" + t.id() + "]", 1);
		Logging::log.message("Path = " + t.path().string(), 1);
		Logging::log.message("Watermark = " + std::to_string(t.watermark()), 1);
		Logging::log.message("", 1);
	}
}
