#include <atomic>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <iostream>
#include <fstream>
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

using namespace ftxui;

std::atomic<int> ally_kills;
std::vector<std::string> ally_champs;
std::atomic<int> enemy_kills;
std::vector<std::string> enemy_champs;
std::atomic<int> game_type;
std::atomic<int> game_time;
std::string update_t_running;
std::string update_current_op;
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

std::string client_status;
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

bool try_connect() {
	auto connection = cpr::GetCallback([&](cpr::Response res) {
		status = res.status_code;
		
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

void update() {
	while (!do_exit) {
		std::this_thread::sleep_for(std::chrono::seconds(3));
		log("Connected to update thread!");
		thread_lock = true;
		log("Getting League Client credentials");
		client_online = false;
		ally_champs.clear();
		enemy_champs.clear();
		in_game = false;
		
		while (!client_online) {
			std::string client_cred;
			std::ifstream fh("C:\\Riot Games\\League of Legends\\lockfile");
			if (fh.is_open()) {
				std::string ln;
				while (std::getline(fh, ln)) {
					if (ln != "") client_cred = ln;
				};
			}
			else {
				std::this_thread::sleep_for(std::chrono::milliseconds(5000));
				continue;
			}
			std::vector<std::string> creds;
			std::stringstream ss(client_cred);
			std::string tmp;
			while (std::getline(ss, tmp, ':')) {
				creds.push_back(tmp);
			}
			client_port = creds[2];
			client_passwd = creds[3];
			client_online = true;
			log("with authentication riot : " + client_passwd);
			log("Found League Client on https://127.0.0.1:" + client_port + "/");
		}

		/*while (champ_sel == false) {
			std::this_thread::sleep_for(std::chrono::milliseconds(2000));
		}*/

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

	auto main_render = Renderer([&] {
		if(status != 0) {
			have_connection = true;
		} else {
			have_connection = false;
		}

		auto client_status_style = color(Color::Purple3) | dim;
		if (client_online) {
			client_status_style = color(Color::Purple3);
		}
		auto update_t_style = color(Color::Purple3) | dim;
		if (thread_lock) {
			update_t_style = color(Color::Purple3);
		}
		if(!in_game) {
			return window(hbox(text("KindredD Monitor") | color(Color::Gold1) | bold, text("")), vbox({
				hbox({
					text("["),
					spinner(2, frame / 2),
					text("] "),
					text("Waiting...") | color(Color::BlueViolet),
				}),
				text("Waiting for League of Legends on https://localhost:2999/") | dim,
				filler(),
				log_buffer()
			}));
		} else {
			return window(hbox(text("KindredD Monitor") | color(Color::Gold1) | bold, text("")), vbox({
					text("League of Legends is running"),
					vbox({
						update_players(),
					}),
					filler(),
					log_buffer()
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
