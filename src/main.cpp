﻿#include <atomic>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <iostream>
#include <fstream>
#include <regex>
#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/dom/elements.hpp>
#include <ctime>
#include <signal.h>
#include <chrono>
#include <locale>
#include <sstream>
#include <thread>
#include "data.hpp"

extern const std::string LOGO_K, LOGO_d, LOGO_D;
extern const std::string champion_data_str;

const std::string version = "v0.3.3";

#ifdef __linux__
std::string platform = "Linux (Generic)";
ftxui::Color platform_color = ftxui::Color::DarkOrange;
#elif _WIN32
std::string platform = "Windows 7/8/10/11";
ftxui::Color platform_color = ftxui::Color::DodgerBlue1;
#elif __APPLE__
std::string platform = "Apple MacOS (Darwin)";
ftxui::Color platform_color = ftxui::Color::Cyan3;
#else
std::string platform = "Unknown (Possibly OpenBSD)";
ftxui::Color platform_color = ftxui::Color::Red3;
#endif


#ifdef _MSC_VER
std::string compiler = "Microsoft Visual Studio (Version " + std::to_string(_MSC_VER) + ")";
#elif __clang__
std::string compiler = "Clang Compiler (Version " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__) + ")";
#elif __GNUC__
std::string compiler = "GNU GCC (Version " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + ")";
#else
std::string compiler = "Unknown (Possibly MinGW or WebAssembly)";
#endif

using namespace ftxui;

std::atomic<int> ally_kills;
std::vector<std::string> ally_champs;
std::atomic<int> enemy_kills;
std::vector<std::string> enemy_champs;
std::atomic<int> game_type;
std::atomic<int> game_time;
std::string current_json_data;
int status;
std::atomic<bool> in_game = false;
std::atomic<bool> do_exit = false;
std::atomic<bool> champ_sel = false;

std::thread *update_t;
bool thread_lock = false;
nlohmann::json players;
nlohmann::json active_player;
std::time_t start_time;
std::string ally_teamname;
nlohmann::json cs_data = {{"kindredd", "running"}};

bool client_online = false;
std::string client_port;
std::string client_passwd;

std::vector<std::string> global_log = { "--:--:--] ", "--:--:--] " };
		
// Game time is in seconds since the start of the game
// Game type conforms to the Riot API spec

void log(std::string line) {
	std::time_t t = std::time(0);
	std::tm* time_now = std::localtime(&t);
	std::string hrs = std::to_string(time_now->tm_hour);
	std::string mins = std::to_string(time_now->tm_min);
	std::string secs = std::to_string(time_now->tm_sec);
	std::string time_str = hrs + ":" + mins + ":" + secs;
	global_log.push_back(time_str + "]" + line);
}

bool send_cs(nlohmann::json json_data) {
	if (json_data == cs_data) return false;
	cs_data = json_data;
	cpr::PostAsync(cpr::Url{ "https://lamb.jacobtye.dev/livegame" },
		cpr::Body{ json_data.dump() },
		cpr::Header{ {"Content-Type", "application/json"} });
	return true;
}

bool try_connect() {
	std::future connection = cpr::GetCallback([&](cpr::Response res) {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		status = res.status_code;
		log(status);
		if (res.status_code != 0 && res.status_code < 400) {
			players = nlohmann::json::parse(res.text);
			cpr::GetCallback([&](cpr::Response res) {
				active_player = nlohmann::json::parse(res.text);
				}, cpr::Url{ "https://127.0.0.1:2999/liveclientdata/activeplayer" },
					cpr::VerifySsl(0));
			in_game = true;
			return true;
		}
		else {
			in_game = false;
			return false;
		}
		}, cpr::Url{ "https://127.0.0.1:2999/liveclientdata/playerlist" },
			cpr::VerifySsl(0));

	while (!connection.valid()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
	return connection.get();
}

void sendall() {
	log("Sending game metadata");
	nlohmann::json gamedata;
	gamedata["event"] = "metadata";
	gamedata["ally"] = {};
	gamedata["ally"]["kills"] = int(ally_kills);
	gamedata["ally"]["champs"] = ally_champs;
	gamedata["enemy"] = {};
	gamedata["enemy"]["kills"] = int(enemy_kills);
	gamedata["enemy"]["champs"] = enemy_champs;
	gamedata["summoner"] = active_player["summonerName"];
	std::time_t time_now = std::time(nullptr);
	gamedata["time"] = time_now;
	current_json_data = gamedata.dump(4);
	cpr::PostAsync(cpr::Url{"https://lamb.jacobtye.dev/livegame"},
			cpr::Body{gamedata.dump()},
			cpr::Header{{"Content-Type", "application/json"}});
}

void sendupdate() {
	log("Updating in real-time");
	nlohmann::json gamedata;
	ally_kills = 0;
	enemy_kills = 0;
	for(int i = 0; i < players.size(); i++) {
		if(players[i]["team"] == ally_teamname) {
			ally_kills += players[i]["scores"]["kills"];
		} else {
			enemy_kills += players[i]["scores"]["kills"];
		}
	}
	gamedata["ally"]["kills"] = int(ally_kills);
	gamedata["enemy"]["kills"] = int(enemy_kills);
	gamedata["event"] = "update";
	std::time_t time_now = time(nullptr);
	gamedata["time"] = time_now - start_time;
	current_json_data = gamedata.dump(4);
	cpr::PostAsync(cpr::Url{"https://lamb.jacobtye.dev/livegame"},
			cpr::Body{gamedata.dump()},
			cpr::Header{{"Content-Type", "application/json"}});
}

void send_game_start() {
	log("Sending game start notification");
	nlohmann::json gamestart;
	gamestart["event"] = "start";
	current_json_data = gamestart.dump(4);
	cpr::PostAsync(cpr::Url{"https://lamb.jacobtye.dev/livegame"},
			cpr::Body{gamestart.dump()},
			cpr::Header{{"Content-Type", "application/json"}});
}

void send_game_end() {
	log("Sending game end notification");
	nlohmann::json gameend;
	gameend["event"] = "end";
	current_json_data = gameend.dump(4);
	cpr::PostAsync(cpr::Url{"https://lamb.jacobtye.dev/livegame"},
			cpr::Body{gameend.dump()},
			cpr::Header{{"Content-Type", "application/json"}});
	in_game = false;
}

int champ_select() {
	bool do_break = false;
	while (!do_break) {
		std::string lcu_url = "https://127.0.0.1:" + client_port;
		auto res = cpr::Get(cpr::Url{ lcu_url + "/lol-champ-select/v1/session" },
			cpr::Authentication{ "riot", client_passwd },
			cpr::VerifySsl(0));

		if (res.status_code == 404) {
			champ_sel = false;
			log("Returning to queue");
			return 2;
		}

		nlohmann::json final_json;
		final_json = {
			{"event", "picks"},
			{"time", ""},
			{"ally", {}},
			{"enemy", {}}
		};

		auto json_res = nlohmann::json::parse(res.text);
		bool doing_bans = true;
		auto actions = nlohmann::json::parse("[]");
		for (int i = 0; i < json_res["actions"].size(); i++) {
			log(std::to_string(i));
			for (int j = 0; j < json_res["actions"][i].size(); j++) {
				actions.push_back(json_res["actions"][i][j]);
			}
		}
		for (int i = 0; i < actions.size(); i++) {
			if (actions[i]["type"] == "pick") doing_bans = false;
		}

		log("Doing bans");
		for (int i = 0; i < actions.size(); i++) {
			auto a = actions[i];
			std::string team = "enemy";
			std::string position;
			if (a["isAllyAction"]) {
				team = "ally";
			}
			if (team == "ally") {
				for (int j = 0; j < json_res["myTeam"].size(); j++) {
					if (json_res["myTeam"][j]["cellId"] == a["actorCellId"]) {
						position = json_res["myTeam"][j]["assignedPosition"];
					}
				}
			}
			else {
				for (int j = 0; j < json_res["theirTeam"].size(); j++) {
					if (json_res["theirTeam"][j]["cellId"] == a["actorCellId"]) {
						position = json_res["theirTeam"][j]["assignedPosition"];
					}
				}
			}
			int champID = a["championId"];
			if (a["completed"]) {
				final_json[team][std::string(actions[i]["type"])].push_back({
					{"position", position}, {"championID", champID}
				});
			} else if(champID != 0) {
				final_json[team]["hover"].push_back({
					{"position", position}, {"championID", champID}
				});
			}
		}
		send_cs(final_json);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	return 0;
}

nlohmann::json get_cmd_opt(std::string commandline) {
	log("Getting cmdline");
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	std::regex re("--app-port=([0-9]+)");
	std::cmatch match;
	nlohmann::json res = {{"status", "online"}};
	if(std::regex_search(commandline.c_str(), match, re)) {
		res["port"] = std::string(match[0]).substr(std::string(match[0]).find("=") + 1);
	} else {
		res["status"] = "offline";
	}
	std::regex pwd("--remoting-auth-token=([a-zA-Z0-9'-_]+)");
	std::cmatch pwd_match;
	if(std::regex_search(commandline.c_str(), pwd_match, pwd)) {
		std::string passwd = std::string(pwd_match[0]).substr(std::string(pwd_match[0]).find("=") + 1);
		passwd.erase(passwd.find_last_not_of(" \n\r\t")+1);
		res["password"] = passwd;
	} else {
		res["status"] = "offline";
	}
	return res;
}

// This section contains the code for finding port numbers etc.
nlohmann::json get_client_data() {
	std::string res;
#ifdef __unix
	// UNIX-only code for finding the port/password for the client
	FILE* ps = popen("ps -Ao cmd | grep LeagueClientUx", "r");
	std::string output;
	if(ps) {
		std::ostringstream stream;
		constexpr std::size_t MAX_LN_SZ = 1024;
		char line[MAX_LN_SZ];
		while(fgets(line, MAX_LN_SZ, ps)) stream << line;
		output = stream.str();
	}
#elif _WIN32
	// Windows-only code for finding the port/password for the client
	/*std::string env_root = getenv("SystemRoot");
	std::string cmdline = env_root + "\\System32\\Wbem\\wmic.exe PROCESS WHERE name='LeagueClientUx.exe' GET commandline";
	log(cmdline);
	FILE * wmic = _popen(cmdline.c_str(), "r");
	std::string output;
	if (wmic) {
		std::ostringstream stream;
		constexpr std::size_t MAX_LN_SZ = 1024;
		char line[MAX_LN_SZ];
		while (fgets(line, MAX_LN_SZ, wmic)) stream << line;
		output = stream.str();
	}*/
	std::string cmdline = "powershell \"Get-CimInstance Win32_Process -Filter 'name LIKE ''LeagueClientUx.exe''' | Select CommandLine | ConvertTo-Json\"";
	FILE* pwsh = _popen(cmdline.c_str(), "r");
	std::string output;
	std::string temp;
	if (pwsh) {
		std::ostringstream stream;
		constexpr std::size_t MAX_LN_SZ = 1024;
		char line[MAX_LN_SZ];
		while (fgets(line, MAX_LN_SZ, pwsh)) stream << line;
		temp = stream.str();
	}
	log("Got output from PWSH");
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	try {
		auto temp_json = nlohmann::json::parse(temp);
		output = temp_json["CommandLine"];
	}
	catch (...) {
		;
	}

#endif
	return get_cmd_opt(output);
}

void update() {
	std::this_thread::sleep_for(std::chrono::milliseconds(2500));
	log("Connected to update thread!");
	thread_lock = true;
	while (!do_exit) {
		ally_champs.clear();
		enemy_champs.clear();
		in_game = false;
		champ_sel = false;
		
		while (!client_online) {
			auto cdata = get_client_data();
			log("CDATA");
			std::this_thread::sleep_for(std::chrono::milliseconds(2500));
			if(cdata["status"] == "offline") {
				std::this_thread::sleep_for(std::chrono::milliseconds(5000));
				continue;
			}

			client_port = cdata["port"];
			client_passwd = cdata["password"];
			client_online = true;
			log("with authentication riot:" + client_passwd);
			log("Found League Client on https://127.0.0.1:" + client_port + "/");
			break;
		}

		std::string lcu_url = "https://127.0.0.1:" + client_port;
		while (!champ_sel) {
			auto res = cpr::Get(cpr::Url{ lcu_url + "/lol-champ-select/v1/session" },
				cpr::Authentication{ "riot", client_passwd },
				cpr::VerifySsl(0));
			if (res.status_code == 200) {
				champ_sel = true;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2000));
		}

		int res = champ_select();

		if(res != 0) {
			log("Attempting to connect to the game...");
			for(int i = 0; i < 4; i++) {
				in_game = try_connect();
				std::this_thread::sleep_for(std::chrono::milliseconds(2000));
			}
			if(!in_game) {
				champ_sel = false;
				continue;
			}
		}
		champ_sel = false;
		while (in_game == false) {
			in_game = try_connect();
			std::this_thread::sleep_for(std::chrono::milliseconds(2000));
		}

		if (in_game) {
			start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			send_game_start();
			for (int i = 0; i < players.size(); i++) {
				if (players[i]["summonerName"] == active_player["summonerName"]) {
					ally_teamname = players[i]["team"];
				}
			}
			for (int i = 0; i < players.size(); i++) {
				if (players[i]["team"] == ally_teamname) {
					ally_champs.push_back(players[i]["championName"]);
				}
				else {
					enemy_champs.push_back(players[i]["championName"]);
				}
			};
			sendall();
			bool do_break = false;
			int gt = 0;
			while (true) {
				sendupdate();
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				gt += 1;
				if (gt % 5 == 0) {
					do_break = !try_connect();
				}
				if (do_break) {
					send_game_end();
					break;
				}
			}
		}
	}
}

int main(int argc, const char* argv[]) {
	std::cout << champion_data_str << std::endl;
	nlohmann::json champion_data = nlohmann::json::parse(champion_data_str);
	log("Waiting for update thread...");
	update_t = new std::thread(update);
	auto screen = ScreenInteractive::Fullscreen();
	int frame = 0;
	int status = 0;
	int wait_secs = 5;

	bool have_connection = false;
	std::time_t prev_time;
	std::locale loc;
	std::string doing_check = "No";

	std::vector<Element> players_disp;

	auto render_debuginfo = [&] {
		return vbox(
			hbox(
				text("KindredD Version "),
				text(version) | color(Color::Gold1),
				text(" running on platform "),
				text(platform) | color(platform_color)
			),
			hbox(
				text(" * ") | color(Color::Gold1),
				text("Compiled with "),
				text(compiler) | color(Color::BlueViolet)
			)
		);
	};

	auto log_buffer = [&] {
		std::string ln_1 = global_log[global_log.size() - 1];
		std::string ln_2 = global_log[global_log.size() - 2];
		std::vector<std::string> ln_1_v;
		std::vector<std::string> ln_2_v;
		std::stringstream ss_1(ln_1);
		std::stringstream ss_2(ln_2);
		std::string tmp;
		while (std::getline(ss_1, tmp, ']')) {
			ln_1_v.push_back(tmp);
		}
		while (std::getline(ss_2, tmp, ']')) {
			ln_2_v.push_back(tmp);
		}
		return vbox(
			hbox(text(ln_1_v[0] + "  "), text(ln_1_v[1]) | color(Color::DarkSeaGreen2)),
			hbox(text(ln_2_v[0] + "  "), text(ln_2_v[1]) | color(Color::DarkSeaGreen2) | dim)
		) | border;
	};

	auto update_players = [&] {
		players_disp.clear();
		std::vector<Element> order;
		std::vector<Element> chaos;
		for(int i = 0; i < players.size(); i++) {
			Color team_color;
			if(players[i]["team"] == "ORDER") {
				team_color = Color::Turquoise2;
			} else {
				team_color = Color::Red3Bis;
			}
			auto player_element = vbox({
				text(std::string(players[i]["summonerName"])) | bold | color(team_color),
				text(std::string(players[i]["championName"])) | bold,
				hbox({
					text(std::to_string(int(players[i]["scores"]["kills"]))),
					text("/"),
					text(std::to_string(int(players[i]["scores"]["deaths"]))),
					text("/"),
					text(std::to_string(int(players[i]["scores"]["assists"]))),
				}),
			}) | border | size(WIDTH, GREATER_THAN, 25);
			if(players[i]["team"] == "ORDER") {
				order.push_back(player_element);
			} else {
				chaos.push_back(player_element);
			}
		}
		return vbox({
			hbox(order),
			hbox(chaos),
		});
	};

	auto render_multiline = [&](std::string txt) {
		std::vector<Element> els;
		std::vector<std::string> lines;
		std::string::size_type pos = 0;
		std::string::size_type prev = 0;
		while ((pos = txt.find('\n', prev)) != std::string::npos) {
			lines.push_back(txt.substr(prev, pos - prev));
			prev = pos + 1;
		}
		lines.push_back(txt.substr(prev));
		for (int i = 0; i < lines.size(); i++) {
			els.push_back(text(lines[i]));
		}
		return vbox(els);
	};

	auto loading_logo = [&] {
		return vbox(
			hbox(
				render_multiline(LOGO_NAME),
				render_multiline(LOGO_D) | color(Color::Gold1)
			) | border | center,
			hbox(text("["), spinner(2, frame / 2), text("]")) | center
		) | center;
	};

	auto cs_render = [&]() {
		std::vector<Element> ally_bans;
		std::vector<Element> ally_hovers;
		std::vector<Element> ally_picks;
		std::vector<Element> enemy_bans;
		std::vector<Element> enemy_hovers;
		std::vector<Element> enemy_picks;
		for (int i = 0; i < cs_data["ally"]["ban"].size(); i++) {
			ally_bans.push_back(hbox(text(std::string(champion_data[std::to_string(int(cs_data["ally"]["ban"][i]["championID"]))]["name"])) | color(Color::Red3), text(" "), text(std::string(champion_data[std::to_string(int(cs_data["ally"]["ban"][i]["championID"]))]["title"])) | dim));
		}
		
		for (int i = 0; i < cs_data["ally"]["pick"].size(); i++) {
			ally_picks.push_back(hbox(text(std::string(champion_data[std::to_string(int(cs_data["ally"]["pick"][i]["championID"]))]["name"])) | color(Color::DodgerBlue1), text(" "), text(std::string(champion_data[std::to_string(int(cs_data["ally"]["pick"][i]["championID"]))]["title"])) | dim));
		}
		for (int i = 0; i < cs_data["ally"]["hover"].size(); i++) {
			ally_hovers.push_back(hbox(text(std::string(champion_data[std::to_string(int(cs_data["ally"]["hover"][i]["championID"]))]["name"])), text(" "), text(std::string(champion_data[std::to_string(int(cs_data["ally"]["hover"][i]["championID"]))]["title"])) | dim));
		}
		for (int i = 0; i < cs_data["enemy"]["ban"].size(); i++) {
			enemy_bans.push_back(hbox(text(std::string(champion_data[std::to_string(int(cs_data["enemy"]["ban"][i]["championID"]))]["name"])) | color(Color::Red3), text(" "), text(std::string(champion_data[std::to_string(int(cs_data["enemy"]["ban"][i]["championID"]))]["title"])) | dim));
		}
		for (int i = 0; i < cs_data["enemy"]["pick"].size(); i++) {
			enemy_picks.push_back(hbox(text(std::string(champion_data[std::to_string(int(cs_data["enemy"]["pick"][i]["championID"]))]["name"])) | color(Color::DodgerBlue1), text(" "), text(std::string(champion_data[std::to_string(int(cs_data["enemy"]["pick"][i]["championID"]))]["title"])) | dim));
		}
		for (int i = 0; i < cs_data["enemy"]["hover"].size(); i++) {
			enemy_hovers.push_back(hbox(text(std::string(champion_data[std::to_string(int(cs_data["enemy"]["hover"][i]["championID"]))]["name"])), text(" "), text(std::string(champion_data[std::to_string(int(cs_data["enemy"]["hover"][i]["championID"]))]["title"])) | dim));
		}
		for(int i = 0; i < 5; i++) {
			if(ally_bans.size() < i) ally_bans.push_back(text(""));
			if(ally_picks.size() < i) ally_picks.push_back(text(""));
			if(ally_hovers.size() < i) ally_hovers.push_back(text(""));
			if(enemy_bans.size() < i) enemy_bans.push_back(text(""));
			if(enemy_picks.size() < i) enemy_picks.push_back(text(""));
			if(enemy_hovers.size() < i) enemy_hovers.push_back(text(""));
		}
		if(champ_sel) {
			return vbox(hbox(window(hbox(text("Ally"), text("")) | color(Color::DarkSeaGreen2), 
				vbox(
					window(text("Bans"),
						hbox(vbox(ally_bans), filler())
					),
					window(text("Hovering"), 
						hbox(vbox(ally_hovers), filler())
					),
					window(text("Picks"), 
						hbox(vbox(ally_picks), filler())
					)
				) | notflex
			) | flex, window(hbox(text("Enemy"), text("")) | color(Color::Red3),
				vbox(
					window(text("Bans"),
						hbox(vbox(enemy_bans), filler())
					),
					window(text("Hovering"), 
						hbox(vbox(enemy_hovers), filler())
					),
					window(text("Picks"), 
						hbox(vbox(enemy_picks), filler())
					)
				) | notflex
			) | flex),
			filler());
		} else {
			return filler();
		}
		return render_multiline(cs_data.dump(4));
	};

	auto client_status_render = [&] {
		if (!client_online) {
			std::string s_text;
			s_text = "Waiting for League Client to start";
			return hbox(
				text("["),
				spinner(2, frame / 2),
				text("] "),
				text(s_text) | dim
			);
		}
		else {
			std::string s_text = "Waiting for champion select on https://localhost:" + client_port + "/";
			return hbox(
				text("[✓] "),
				text(s_text) | color(Color::DarkSeaGreen2)
			);
		}
	};

	auto main_render = Renderer([&] {
		if(status != 0) {
			have_connection = true;
		} else {
			have_connection = false;
		}

		if(!thread_lock) {
			return window(hbox(text("KindredD Monitor") | color(Color::Gold1) | bold, text("")), vbox(
				filler(),
				loading_logo() | center,
				filler(),
				log_buffer(),
				render_debuginfo()
			)
		);
		}

		if(!in_game) {
			return window(hbox(text("KindredD Monitor") | color(Color::Gold1) | bold, text("")), vbox({
				text("Waiting...") | color(Color::BlueViolet),
				hbox(
					text("["),
					spinner(2, frame / 2),
					text("] "),
					text("Waiting for League of Legends on https://localhost:2999/") | dim
				),
				client_status_render(),
				cs_render(),
				filler(),
				log_buffer(),
				render_debuginfo()
			}));
		} else {
			return window(hbox(text("KindredD Monitor") | color(Color::Gold1) | bold, text("")), vbox({
					text("[✓] League of Legends is running") | color(Color::DarkSeaGreen2),
					vbox({
						update_players(),
					}),
					filler(),
					log_buffer(),
					render_debuginfo()
				}));
		}
	});
	bool refresh_ui_continue = true;
	std::thread refresh_ui([&] {
	    	while (refresh_ui_continue) {
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(0.05s);
	      		frame++;
  	    		screen.PostEvent(Event::Custom);
   		 }
  	});
	screen.Loop(main_render);
	return 0;
}
