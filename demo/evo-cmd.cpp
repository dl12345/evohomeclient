/*
 * Copyright (c) 2018 Gordon Bos <gordon@bosvangennip.nl> All rights reserved.
 *
 * Simple one-way app for sending mode changes to Evohome web portal
 *
 *
 * Source code subject to GNU GENERAL PUBLIC LICENSE version 3
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <cstring>
#include <time.h>
#include <stdlib.h>
#include "../evohomeclient/evohomeclient.h"


#ifndef MYNAME
#define MYNAME "evo-cmd"
#endif

#ifndef MYPATH
#define MYPATH "/"
#endif

#ifndef CONF_FILE
#define CONF_FILE "evoconfig"
#endif


#ifdef _WIN32
#define localtime_r(timep, result) localtime_s(result, timep)
#define gmtime_r(timep, result) gmtime_s(result, timep)
#endif

#ifndef _WIN32
#define sprintf_s(buffer, buffer_size, stringbuffer, ...) (sprintf(buffer, stringbuffer, __VA_ARGS__))
#endif

using namespace std;

time_t now;
bool dobackup, verbose, dolog;
std::string command, backupfile, configfile, szERROR, szWARN, logfile, szLOG;
std::map<std::string,std::string> evoconfig;
map<int,std::string> parameters;
ofstream flog;

void init_globals()
{
	configfile = CONF_FILE;

	now = time(0);
	dobackup = true;
	verbose = false;
	command = "";

	szERROR = "ERROR: ";
	szWARN = "WARNING: ";

	dolog = false;
	logfile = "";
	szLOG = "";
}


void usage(std::string mode)
{
	if (mode == "badparm")
	{
		cerr << "Bad parameter\n";
		exit(1);
	}
	if (mode == "missingfile")
	{
		cerr << "You need to supply a backupfile\n";
		exit(1);
	}
	if (mode == "short")
	{
		cout << "Usage: " << MYNAME << " [-hv] [-c file] [-l file] [-b|-r file]\n";
		cout << "Type \"" << MYNAME << " --help\" for more help\n";
		exit(0);
	}
	cout << "Usage: " << MYNAME << " [OPTIONS]\n";
	cout << endl;
	cout << "  -h, --help                display this help and exit\n";
	cout << "  -b, --backup  <FILE>      create a schedules backup\n";
	cout << "  -r, --restore <FILE>      restore a schedules backup\n";
	cout << "  -v, --verbose             print a lot of information\n";
	cout << "  -c, --conf=FILE           use FILE for server settings and credentials\n";
	cout << "  -l, --log=FILE            use FILE for logging (implies -v)\n";
	cout << "      --set-mode <PARMS>    set system mode\n";
	cout << "      --set-temp <PARMS>    set or cancel temperature override\n";
	cout << "      --set-dhw  <PARMS>    set or cancel domestic hot water override\n";
	cout << endl;
	cout << "Parameters for set arguments:\n";
	cout << " --set-mode <system mode> [time until] [+duration]\n";
	cout << " --set-temp <zone id> <setpoint mode> <target temperature> [time until] [+duration]\n";
	cout << " --set-dhw  <dhw id> <dhw status> [time until] [+duration]\n";
	cout << endl;
	exit(0);
}


void print_out(std::string message)
{
	if (dolog)
		flog << message << endl;
	cout << message << endl;
}


void print_err(std::string message)
{
	if (dolog)
		flog << message << endl;
	cerr << message << endl;
}


bool read_evoconfig(std::string configfile)
{
	ifstream myfile (configfile.c_str());
	if ( myfile.is_open() )
	{
		stringstream key,val;
		bool isKey = true;
		bool quoted = false;
		string line;
		unsigned int i;
		while ( getline(myfile,line) )
		{
			if ( (line[0] == '#') || (line[0] == ';') )
				continue;
			for (i = 0; i < line.length(); i++)
			{
				if ( (line[i] == '\'') || (line[i] == '"') )
				{
					quoted = ( ! quoted );
					continue;
				}
				if (line[i] == 0x0d)
					continue;
				if ( (line[i] == ' ') && ( ! quoted ) )
					continue;
				if (line[i] == '=')
				{
					isKey = false;
					continue;
				}
				if (isKey)
					key << line[i];
				else
				{
					if ( (line[i] == ' ') && (key.str() == "srt") )
						print_err("WARNING: space detected in 'srt' setting. Controlling Evohome from Domoticz will likely not function");
					val << line[i];
				}
			}
			if ( ! isKey )
			{
				string skey = key.str();
				evoconfig[skey] = val.str();
				isKey = true;
				key.str("");
				val.str("");
			}
		}
		myfile.close();
		return true;
	}
	return false;
}


void startlog(std::string fname)
{
	logfile = fname;
	flog.open(logfile.c_str(), ios::out | ios::app);
	if (flog.is_open())
	{
		dolog = true;
		struct tm ltime;
		localtime_r(&now, &ltime);
		char c_until[40];
		sprintf_s(c_until, 40, "%04d-%02d-%02d %02d:%02d:%02d", ltime.tm_year+1900, ltime.tm_mon+1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
		flog << MYNAME << " start: " << c_until << endl;
		return;
	}
	cerr << "WARNING: cannot open logfile '" << logfile << "'\n";
	dolog = false;
	verbose = true;
}


void log(std::string message)
{
	if (dolog)
	{
		flog << message << endl;
		return;
	}
	if (verbose)
		cout << message << endl;
}


void stoplog()
{
	if (!dolog)
		return;
	flog << "--" << endl;
	flog.close();
}


void exit_error(std::string message)
{
	print_err(message);
	stoplog();
	exit(1);
}


EvohomeClient::temperatureControlSystem* select_temperatureControlSystem(EvohomeClient &eclient)
{
	int location = 0;
	int gateway = 0;
	int temperatureControlSystem = 0;
	bool is_unique_heating_system = false;
	if ( evoconfig.find("location") != evoconfig.end() ) {
		log(szLOG+"using location from "+configfile);
		int l = eclient.locations.size();
		location = atoi(evoconfig["location"].c_str());
		if (location > l)
			exit_error(szERROR+"the Evohome location specified in "+configfile+" cannot be found");
		is_unique_heating_system = ( (eclient.locations[location].gateways.size() == 1) &&
						(eclient.locations[location].gateways[0].temperatureControlSystems.size() == 1)
						);
	}
	if ( evoconfig.find("gateway") != evoconfig.end() ) {
		log(szLOG+"using gateway from "+configfile);
		int l = eclient.locations[location].gateways.size();
		gateway = atoi(evoconfig["gateway"].c_str());
		if (gateway > l)
			exit_error(szERROR+"the Evohome gateway specified in "+configfile+" cannot be found");
		is_unique_heating_system = (eclient.locations[location].gateways[gateway].temperatureControlSystems.size() == 1);
	}
	if ( evoconfig.find("controlsystem") != evoconfig.end() ) {
		log(szLOG+"using controlsystem from "+configfile);
		int l = eclient.locations[location].gateways[gateway].temperatureControlSystems.size();
		temperatureControlSystem = atoi(evoconfig["controlsystem"].c_str());
		if (temperatureControlSystem > l)
			exit_error(szERROR+"the Evohome temperature controlsystem specified in "+configfile+" cannot be found");
		is_unique_heating_system = true;
	}

	if ( ! is_unique_heating_system)
		return NULL;

	return &eclient.locations[location].gateways[gateway].temperatureControlSystems[temperatureControlSystem];
}


void parse_args(int argc, char** argv) {
	int i=1;
	int p=1;
	std::string word;
	while (i < argc) {
		word = argv[i];
		if (word.length() > 1 && word[0] == '-' && word[1] != '-') {
			for (size_t j=1;j<word.length();j++) {
				if (word[j] == 'h')
					usage("short");
				else if (word[j] == 'b')
				{
					if (j+1 < word.length())
						usage("badparm");
					command = "backup";
					dobackup = true;
					i++;
					if (i >= argc)
						usage("missingfile");
					backupfile = argv[i];
				}
				else if (word[j] == 'r')
				{
					if (j+1 < word.length())
						usage("badparm");
					command = "backup";
					dobackup = false;
					i++;
					if (i >= argc)
						usage("missingfile");
					backupfile = argv[i];
				}
				else if (word[j] == 'v')
					verbose = true;
				else if (word[j] == 'c')
				{
					if (j+1 < word.length())
						usage("badparm");
					i++;
					if (i >= argc)
						usage("badparm");
					configfile = argv[i];
				}
				else if (word[j] == 'l')
				{
					if (j+1 < word.length())
						usage("badparm");
					i++;
					if (i >= argc)
						usage("badparm");
					startlog(argv[i]);
				}
				else
					usage("badparm");
			}
		}
		else if (word == "--help")
			usage("long");
		else if (word == "--backup")
		{
			command = "backup";
			dobackup = true;
			i++;
			if (i >= argc)
				usage("missingfile");
			backupfile = argv[i];
		}
		else if (word == "--restore")
		{
			command = "backup";
			dobackup = false;
			i++;
			if (i >= argc)
				usage("missingfile");
			backupfile = argv[i];
		}
		else if (word == "--verbose")
			verbose = true;
		else if (word.substr(0,7) == "--conf=")
			configfile = word.substr(7);
		else if (word.substr(0,6) == "--log=")
			startlog(word.substr(6));
		else if (word.substr(0,6) == "--set-")
		{
			command = word.substr(2);
			i++;
			while ( (i < argc) && (argv[i][0] != '-') )
			{
				parameters[p] = argv[i];
				i++;
				p++;
			}
			continue;
		}
		else
			usage("badparm");
		i++;
	}
}


void cmd_backup_and_restore_schedules()
{
	// connect to Evohome server
	log("connect to Evohome server");
	EvohomeClient eclient = EvohomeClient(evoconfig["usr"],evoconfig["pw"]);

	// retrieve Evohome installation
	log("retrieve Evohome installation info");
	eclient.full_installation();

	if (dobackup)	// backup
	{
		print_out("Start backup of Evohome schedules");
		if ( ! eclient.schedules_backup(backupfile) )
			exit_error(szERROR+"failed to open backup file '"+backupfile+"'");
		print_out("Done!");
	}
	else		// restore
	{
		print_out("Start restore of Evohome schedules");
		if ( ! eclient.schedules_restore(backupfile) )
			exit_error(szERROR+"failed to open backup file '"+backupfile+"'");
		print_out("Done!");
	}

	eclient.cleanup();
}


std::string format_time(std::string utc_time)
{
	struct tm ltime;
	if (utc_time[0] == '+')
	{
		int minutes = atoi(utc_time.substr(1).c_str());
		gmtime_r(&now, &ltime);
		ltime.tm_min += minutes;
		
	}
	else if (utc_time.length() < 19)
		exit_error(szERROR+"bad timestamp value on command line");
	else
	{
		ltime.tm_year = atoi(utc_time.substr(0, 4).c_str()) - 1900;
		ltime.tm_mon = atoi(utc_time.substr(5, 2).c_str()) - 1;
		ltime.tm_mday = atoi(utc_time.substr(8, 2).c_str());
		ltime.tm_hour = atoi(utc_time.substr(11, 2).c_str());
		ltime.tm_min = atoi(utc_time.substr(14, 2).c_str());
		ltime.tm_sec = atoi(utc_time.substr(17, 2).c_str());
	}
	ltime.tm_isdst = -1;
	time_t ntime = mktime(&ltime);
	if ( ntime == -1)
		exit_error(szERROR+"bad timestamp value on command line");
	char c_until[40];
	sprintf_s(c_until, 40, "%04d-%02d-%02dT%02d:%02d:%02dZ", ltime.tm_year+1900, ltime.tm_mon+1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
	return string(c_until);
}


void cancel_temperature_override()
{
	log("connect to Evohome server");
	EvohomeClient eclient = EvohomeClient(evoconfig["usr"],evoconfig["pw"]);

	if ( ! eclient.cancel_temperature_override(parameters[1]) )
		exit_error(szERROR+"failed to cancel override for zone "+parameters[1]);

	eclient.cleanup();
	log("Done!");
}


void cmd_set_temperature()
{
	if ( (parameters.size() > 1) && (parameters[2] == "0") )
	{
		cancel_temperature_override();
		exit(0);
	}
	if ( (parameters.size() < 3) || (parameters.size() > 4) )
		usage("badparm");

	std::string s_until = "";
	if (parameters.size() == 4)
		s_until = format_time(parameters[4]);


	log("connect to Evohome server");
	EvohomeClient eclient = EvohomeClient(evoconfig["usr"],evoconfig["pw"]);

	log("set target temperature");
	eclient.set_temperature(parameters[1], parameters[3], s_until);

	eclient.cleanup();
	log("Done!");
}


void cmd_set_system_mode()
{
	if ( (parameters.size() == 0) || (parameters.size() > 2) )
		usage("badparm");
	std::string systemId;
	std::string until = "";
	std::string mode = parameters[1];
	if (parameters.size() == 2)
		until = format_time(parameters[2]);
	log("connect to Evohome server");
	EvohomeClient eclient = EvohomeClient(evoconfig["usr"],evoconfig["pw"]);
	if ( evoconfig.find("systemId") != evoconfig.end() ) {
		log(szLOG+"using systemId from "+configfile);
		systemId = evoconfig["systemId"];
	}
	else
	{
		log("retrieve Evohome installation info");
		eclient.full_installation();

		// set Evohome heating system
		EvohomeClient::temperatureControlSystem* tcs = NULL;
		if (eclient.is_single_heating_system())
			tcs = &eclient.locations[0].gateways[0].temperatureControlSystems[0];
		else
			select_temperatureControlSystem(eclient);
		if (tcs == NULL)
			exit_error(szERROR+"multiple Evohome systems found - don't know which one to use");
		systemId = tcs->systemId;
	}
	if ( ! eclient.set_system_mode(systemId, mode, until) )
		exit_error(szERROR+"failed to set system mode to "+mode);
	log("updated system status");
	eclient.cleanup();
}


void cmd_set_dhw_state()
{
	if ( (parameters.size() < 2) || (parameters.size() > 3) )
		usage("badparm");
	if ( (parameters[2] != "on") && (parameters[2] != "off") && (parameters[2] != "auto") )
		usage("badparm");

	std::string s_until = "";
	if (parameters.size() == 3)
		s_until = format_time(parameters[3]);

	log("connect to Evohome server");
	EvohomeClient eclient = EvohomeClient(evoconfig["usr"],evoconfig["pw"]);
	log("set domestic hot water state");
	eclient.set_dhw_mode(parameters[1], parameters[2], s_until);
	eclient.cleanup();
	log("Done!");
}


std::string getpath(std::string filename)
{
#ifdef _WIN32
	stringstream ss;
	unsigned int i;
	for (i = 0; i < filename.length(); i++)
	{
		if (filename[i] == '\\')
			ss << '/';
		else
			ss << filename[i];
	}
	filename = ss.str();
#endif
	std::size_t pos = filename.rfind('/');
	if (pos == std::string::npos)
		return "";
	return filename.substr(0, pos+1);
}


int main(int argc, char** argv)
{
	init_globals();
	parse_args(argc, argv);

	if (command.empty())
		usage("long");

	if ( ( ! read_evoconfig(configfile) ) && (getpath(configfile) == "") )
	{
		// try to find evoconfig in the application path
		stringstream ss;
		ss << getpath(argv[0]) << configfile;
		if ( ! read_evoconfig(ss.str()) )
			exit_error(szERROR+"can't read config file");
	}

	if ( ( ! dolog ) && (evoconfig.find("logfile") != evoconfig.end()) )
		startlog(evoconfig["logfile"]);

	else if (command == "backup")
	{
		cmd_backup_and_restore_schedules();
	}

	else if (command == "set-mode")
	{
		cmd_set_system_mode();
	}

	else if (command == "set-temp")
	{
		cmd_set_temperature();
	}
	
	else if (command == "set-dhw")
	{
		cmd_set_dhw_state();
	}


	stoplog();
	return 0;
}

