#include <atomic>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <iostream>
#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/dom/elements.hpp>
#include <ctime>
#include <threads.h>
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
bool in_game = false;
bool thread_lock = false;
std::thread update_t;
nlohmann::json players;
nlohmann::json active_player;
std::time_t start_time;
std::string ally_teamname;
		
// Game time is in seconds since the start of the game
// Game type conforms to the Riot API spec


void sendall() {
	nlohmann::json gamedata;
	gamedata["ally"] = {};
	gamedata["ally"]["kills"] = int(ally_kills);
	gamedata["ally"]["champs"] = {};
	for(int i = 0; i < ally_champs.size(); i++) {
		gamedata["ally"]["champs"][i] = ally_champs[i];
	};
	gamedata["enemy"] = {};
	gamedata["enemy"]["kills"] = int(enemy_kills);
	gamedata["enemy"]["champs"] = {};
	for(int i = 0; i < enemy_champs.size(); i++) {
		gamedata["enemy"]["champs"][i] = enemy_champs[i];
	}
	std::string gt;
	std::map<std::string, int> gt_map { {"A", 2} };
	switch(game_type) {
		case 400:
			gt = "DRAFT 5v5";
			break;
		case 420:
			gt = "RANKED SOLO/DUO 5v5";
			break;
		case 430:
			gt = "BLIND 5v5";
			break;
		case 440:
			gt = "RANKED FLEX 5v5";
			break;
		case 450:
			gt = "HA ARAM 5v5";
			break;
		default:
			gt = "LIMITED-TIME GAMEMODE";
			break;
	}
	gamedata["gametype"] = gt;
	int time_now = time(nullptr) - start_time;
	gamedata["time"] = time_now;
	cpr::PostAsync(cpr::Url{"https://lamb.jacobtye.dev/livegame"},
			cpr::Body{gamedata.dump()});
}

void sendupdate() {
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
	gamedata["allykills"] = int(ally_kills);
	gamedata["enemykills"] = int(enemy_kills);
	std::time_t time_now = time(nullptr);
	gamedata["time"] = time_now - start_time;
	cpr::PostAsync(cpr::Url{"https://lamb.jacobtye.dev/livegame"},
			cpr::Body{gamedata.dump()});
}

void send_game_start() {
	nlohmann::json gamestart;
	gamestart["event"] = "start";
	cpr::PostAsync(cpr::Url{"https://lamb.jacobtye.dev/livegame"},
			cpr::Body{gamestart.dump()});
}

void send_game_end() {
	nlohmann::json gameend;
	gameend["event"] = "end";
	cpr::PostAsync(cpr::Url{"https://lamb.jacobtye.dev/livegame"},
			cpr::Body{gameend.dump()},
			cpr::Header{{"Content-Type", "application/json"}});
}

void handle_ctl_c(int a) {
	update_t.join();
}

void update() {
	if(in_game == true) {
		start_time = time(nullptr);
		send_game_start();
		for(int i = 0; i < players.size(); i++) {
			if(players[i]["summonerName"] == active_player["summonerName"]) {
				ally_teamname = players[i]["team"];
			}
		}
		for(int i = 0; i < players.size(); i++) {
			if(players[i]["team"] == ally_teamname) {
				ally_champs.push_back(players[i]["championName"]);
			} else {
				enemy_champs.push_back(players[i]["championName"]);
			}
		};
		sendall();
		while(in_game == true) {
			sendupdate();
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			game_time += 1;
		}
		send_game_end();
	}
}

int main(int argc, const char* argv[]) {
	auto screen = ScreenInteractive::Fullscreen();
	int frame = 0;
	int status = 0;
	int wait_secs = 5;

	signal(SIGINT, handle_ctl_c);

	bool have_connection = false;
	std::time_t prev_time;
		std::locale loc;
	std::string doing_check = "No";
	
	auto try_connect = [&] {
		std::time_t time_now = time(nullptr);
		if(int(time_now) - int(prev_time) >= wait_secs) {
			doing_check = "Yes";
			auto connection = cpr::GetCallback([&](cpr::Response res) {
				status = res.status_code;
				if(res.status_code != 0 && res.status_code < 400) {
					wait_secs = 1;
					players = nlohmann::json::parse(res.text);
					prev_time = time_now;
					in_game = true;
					cpr::GetCallback([&] (cpr::Response res) {
						active_player = nlohmann::json::parse(res.text);
					}, cpr::Url{"https://127.0.0.1:2999/liveclientdata/activeplayer"},
					cpr::VerifySsl(false));
				} else {
					in_game = false;
				}
			}, cpr::Url{"https://127.0.0.1:2999/liveclientdata/playerlist"},
			cpr::VerifySsl(false));
			prev_time = time_now;
		} else {
			doing_check = "No";
		};
		return(text(""));
	};

	std::vector<Element> players_disp;

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

	auto main_render = Renderer([&] {
		if(status != 0) {
			have_connection = true;
		} else {
			have_connection = false;
		}
		if(in_game && thread_lock == false) {
			thread_lock = true;
			update_t = std::thread(update);
		} else if(!in_game && thread_lock == true) {
			;
		}
		if(!have_connection) {
			return window(hbox(text("KindredD Monitor") | color(Color::Gold1) | bold, text("")), vbox({
				hbox({
					text("["),
					spinner(2, frame / 2),
					text("] "),
					text("Waiting...") | color(Color::BlueViolet),
				}),
				text("Waiting for League Client on https://localhost:2999/") | dim,
				hflow(try_connect()),
			}));
		} else {
			return vbox({
				window(hbox(text("KindredD Monitor") | color(Color::Gold1) | bold, text("")), vbox({
					text("League of Legends is running"),
					vbox({
						update_players(),
					}),
					text(std::to_string(status)),
					text(doing_check),
					hflow(try_connect()),
				}))
			});
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
	update_t.join();
	return 0;
}
