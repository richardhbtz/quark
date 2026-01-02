module ws

extern "C" {
	int ws_create();
	int ws_connect(int, str, str[], int); // handle, url, headers, header_count
	int ws_connect_simple(int, str);
	int ws_is_open(int);
	int ws_send_text(int, str);
	str ws_recv(int, int); // timeout_ms
	void ws_close(int, int, str);
	void ws_destroy(int);
	str ws_last_error(int);
	int ws_last_close_code(int);
	void ws_clear_last_error(int);
	void ws_free(str);
	int time_ms(); // Current time in milliseconds (for timing/heartbeats)
}

struct Ws {
	data { handle: int }

	Ws new() {
		var h = ws_create();
		ret Ws { handle: h };
	}

	bool connect(url: str) {
		ret ws_connect_simple(this.handle, url) != 0;
	}

	bool connect_with_headers(url: str, headers: str[]) {
		ret ws_connect(this.handle, url, headers, headers.length()) != 0;
	}

	bool is_open() { ret ws_is_open(this.handle) != 0; }

	bool send(text: str) { ret ws_send_text(this.handle, text) != 0; }

	str recv(timeout_ms: int) { ret ws_recv(this.handle, timeout_ms); }

	void close(code: int, reason: str) { ws_close(this.handle, code, reason); }

	void destroy() { ws_destroy(this.handle); }

	str last_error() { ret ws_last_error(this.handle); }

	int last_close_code() { ret ws_last_close_code(this.handle); }

	void clear_error() { ws_clear_last_error(this.handle); }
}

// Example usage:
// var ws = Ws.new();
// var ok = ws.connect("wss://echo.websocket.org", {});
// if ok { ws.send("hello"); var msg = ws.recv(5000); print("{}", msg); ws.close(1000, "bye"); }

