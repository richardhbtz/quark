//EXPECT: echo: hello
//EXPECT: done

import ws

int main() {
    var ws = Ws.new();
    var ok = ws.connect("wss://ws.ifelse.io");
    if (!ok) {
        print("connect failed: {}", ws.last_error());
        ret 1;
    }
    var tries = 0;
    // Wait up to ~10s for open, ticking recv to yield
    while (tries < 100 && !ws.is_open()) { var _ = ws.recv(100); tries = tries + 1; }

    ws.send("hello");
    var got = false;
    var attempts = 10; // up to ~5s total
    while (attempts > 0 && !got) {
        var msg = ws.recv(500);
        if (msg.length() > 0) {
            // Some echo servers send a greeting first; ignore until we see our echo
            if (msg == "hello") {
                print("echo: {}", msg);
                got = true;
            }
        }
        attempts = attempts - 1;
    }
    ws.close(1000, "bye");
    print("done");
    ret 0;
}
