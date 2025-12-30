#include "quark_ws.h"
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXNetSystem.h>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <deque>
#include <string>
#include <memory>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <utility>

struct WsClient {
	std::unique_ptr<ix::WebSocket> socket;
	std::mutex mtx;
	std::condition_variable cv;
	std::deque<std::string> messages;
	std::string last_error;
	std::atomic<bool> open{false};
	std::atomic<int> last_close_code{0};
};

static std::once_flag g_ix_init_once;
static void ensure_ix_init() {
	std::call_once(g_ix_init_once, [](){ ix::initNetSystem(); });
}

static std::mutex g_map_mtx;
static std::unordered_map<int, std::shared_ptr<WsClient>> g_clients;
static std::atomic<int> g_next_id{1};

static std::shared_ptr<WsClient> get_client(int handle) {
	std::lock_guard<std::mutex> lock(g_map_mtx);
	auto it = g_clients.find(handle);
	if (it == g_clients.end()) return nullptr;
	return it->second;
}
 

extern "C" {

int ws_create() {
	ensure_ix_init();
	auto client = std::make_shared<WsClient>();
	client->socket = std::make_unique<ix::WebSocket>();
	int id = g_next_id.fetch_add(1);
	{
		std::lock_guard<std::mutex> lock(g_map_mtx);
		g_clients[id] = client;
	}
	return id;
}

int ws_connect(int handle, const char* url, const char* const* headers, int header_count) {
	auto client = get_client(handle);
	if (!client || !url) return 0;

	try {
		client->socket->setUrl(url);

		// Enable automatic ping/pong to keep connection alive
		client->socket->setPingInterval(30); // Send ping every 30 seconds
		client->socket->enablePong(); // Respond to server pings

		// If the URL uses wss://, enable TLS options so IXWebSocket will use system
		// certificates on platforms like macOS. By default ixwebsocket may require
		// explicit TLS options for some builds, so set caFile="SYSTEM" here.
		try {
			std::string u(url);
			if (u.rfind("wss://", 0) == 0) {
				ix::SocketTLSOptions tlsOpts;
				tlsOpts.caFile = "SYSTEM"; // use system CA bundle
				// Explicitly flag TLS usage so backend selects secure transport
				tlsOpts.tls = true;
				client->socket->setTLSOptions(tlsOpts);
			}
		} catch (...) {
			// Non-fatal: if TLS options aren't available or fail, continue and let
			// the connection attempt report errors via the usual path.
		}

		if (headers && header_count > 0) {
			ix::WebSocketHttpHeaders h;
			for (int i = 0; i < header_count; ++i) {
				if (!headers[i]) continue;
				std::string line(headers[i]);
				auto pos = line.find(':');
				if (pos != std::string::npos) {
					std::string name = line.substr(0, pos);
					// skip space after colon if present
					std::string value = line.substr(pos + 1);
					if (!value.empty() && value[0] == ' ') value.erase(0, 1);
					h[name] = value;
				}
			}
			client->socket->setExtraHeaders(h);
		}

		client->socket->setOnMessageCallback([client](const ix::WebSocketMessagePtr& msg){
			switch (msg->type) {
				case ix::WebSocketMessageType::Open:
					client->open.store(true);
					break;
				case ix::WebSocketMessageType::Close:
					client->open.store(false);
					client->last_close_code.store(static_cast<int>(msg->closeInfo.code));
					break;
				case ix::WebSocketMessageType::Error:
					client->last_error = msg->errorInfo.reason;
					break;
				case ix::WebSocketMessageType::Message:
					if (msg->binary) {
						// Ignore binary for now
						break;
					}
					{
						std::lock_guard<std::mutex> lg(client->mtx);
						client->messages.emplace_back(msg->str);
						client->cv.notify_one();
					}
					break;
				default:
					break;
			}
		});

		client->socket->start();
		return 1;
	} catch (const std::exception& e) {
		client->last_error = e.what();
		return 0;
	}
}

int ws_connect_simple(int handle, const char* url) {
	return ws_connect(handle, url, nullptr, 0);
}

int ws_is_open(int handle) {
	auto client = get_client(handle);
	if (!client) return 0;
	return client->open.load() ? 1 : 0;
}

int ws_send_text(int handle, const char* text) {
	auto client = get_client(handle);
	if (!client || !text) return 0;
	try {
		auto res = client->socket->sendText(text);
		return res.success ? 1 : 0;
	} catch (...) {
		return 0;
	}
}

char* ws_recv(int handle, int timeout_ms) {
	auto client = get_client(handle);
	if (!client) {
		char* s = (char*)std::malloc(1); if (s) s[0] = '\0'; return s;
	}

	std::unique_lock<std::mutex> lk(client->mtx);
	if (client->messages.empty()) {
		if (timeout_ms > 0) {
			client->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]{ return !client->messages.empty(); });
		}
	}

	if (client->messages.empty()) {
		char* s = (char*)std::malloc(1); if (s) s[0] = '\0'; return s;
	}

	std::string msg = std::move(client->messages.front());
	client->messages.pop_front();
	lk.unlock();

	char* out = (char*)std::malloc(msg.size() + 1);
	if (!out) { char* s = (char*)std::malloc(1); if (s) s[0] = '\0'; return s; }
	std::memcpy(out, msg.data(), msg.size());
	out[msg.size()] = '\0';
	return out;
}

void ws_close(int handle, int code, const char* reason) {
	auto client = get_client(handle);
	if (!client) return;
	try {
		std::string r = reason ? reason : std::string();
		client->socket->close(static_cast<uint16_t>(code), r);
	} catch (...) {
	}
}

void ws_destroy(int handle) {
	std::shared_ptr<WsClient> client;
	{
		std::lock_guard<std::mutex> lock(g_map_mtx);
		auto it = g_clients.find(handle);
		if (it != g_clients.end()) { client = it->second; g_clients.erase(it); }
	}
	if (client) {
		try { client->socket->stop(); } catch (...) {}
	}
}

const char* ws_last_error(int handle) {
	auto client = get_client(handle);
	static thread_local std::string empty;
	if (!client) { empty.clear(); return empty.c_str(); }
	return client->last_error.c_str();
}

int ws_last_close_code(int handle) {
	auto client = get_client(handle);
	if (!client) return 0;
	return client->last_close_code.load();
}

void ws_clear_last_error(int handle) {
	auto client = get_client(handle);
	if (!client) return;
	client->last_error.clear();
}

void ws_free(char* s) {
	if (s) std::free(s);
}

} // extern "C"

