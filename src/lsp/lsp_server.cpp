#include "lsp/lsp_server.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <regex>




namespace lsp {

namespace json {

std::string escape(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

std::string unescape(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '"': result += '"'; i++; break;
                case '\\': result += '\\'; i++; break;
                case 'n': result += '\n'; i++; break;
                case 'r': result += '\r'; i++; break;
                case 't': result += '\t'; i++; break;
                default: result += s[i]; break;
            }
        } else {
            result += s[i];
        }
    }
    return result;
}

std::string getString(const std::string& json, const std::string& key) {
    
    std::string keyPattern = "\"" + key + "\"";
    size_t keyPos = json.find(keyPattern);
    if (keyPos == std::string::npos) return "";
    
    
    size_t colonPos = json.find(':', keyPos + keyPattern.size());
    if (colonPos == std::string::npos) return "";
    
    
    size_t start = colonPos + 1;
    while (start < json.size() && (json[start] == ' ' || json[start] == '\t' || json[start] == '\n' || json[start] == '\r')) {
        start++;
    }
    
    
    if (start >= json.size() || json[start] != '"') return "";
    start++;
    
    
    std::string value;
    while (start < json.size() && json[start] != '"') {
        if (json[start] == '\\' && start + 1 < json.size()) {
            start++;
            char escaped = json[start];
            switch (escaped) {
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case '/': value += '/'; break;
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                default: value += escaped; break;
            }
        } else {
            value += json[start];
        }
        start++;
    }
    
    return value;
}


std::string getId(const std::string& json) {
    
    std::regex reStr("\"id\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch matchStr;
    if (std::regex_search(json, matchStr, reStr) && matchStr.size() > 1) {
        return "\"" + matchStr[1].str() + "\"";
    }
    
    std::regex reInt("\"id\"\\s*:\\s*(-?\\d+)");
    std::smatch matchInt;
    if (std::regex_search(json, matchInt, reInt) && matchInt.size() > 1) {
        return matchInt[1].str();
    }
    return "";
}

int getInt(const std::string& json, const std::string& key, int defaultVal = 0) {
    std::string pattern = "\"" + key + "\"\\s*:\\s*(-?\\d+)";
    std::regex re(pattern);
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        try {
            return std::stoi(match[1].str());
        } catch (...) {}
    }
    return defaultVal;
}

std::string getObject(const std::string& json, const std::string& key) {
    size_t keyPos = json.find("\"" + key + "\"");
    if (keyPos == std::string::npos) return "";
    
    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) return "";
    
    size_t start = json.find('{', colonPos);
    if (start == std::string::npos) return "";
    
    int depth = 1;
    size_t end = start + 1;
    while (end < json.size() && depth > 0) {
        if (json[end] == '{') depth++;
        else if (json[end] == '}') depth--;
        else if (json[end] == '"') {
            end++;
            while (end < json.size() && json[end] != '"') {
                if (json[end] == '\\') end++;
                end++;
            }
        }
        end++;
    }
    
    return json.substr(start, end - start);
}

} 





LspServer::LspServer() {}

LspServer::~LspServer() {}

void LspServer::run() {
    log("Quark LSP server starting...");
    
    while (!shutdownRequested_) {
        std::string message = readMessage();
        if (message.empty()) {
            log("Empty message received, exiting...");
            break;
        }
        
        log("Received message: " + message.substr(0, std::min<size_t>(100, message.size())));
        
        std::string response = processMessage(message);
        if (!response.empty()) {
            log("Sending response: " + response.substr(0, std::min<size_t>(100, response.size())));
            sendMessage(response);
        }
    }
    
    log("Quark LSP server shutting down");
}

std::string LspServer::readMessage() {
    
    std::string headers;
    int contentLength = 0;
    
    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return "";
        }
        
        
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) {
            break;
        }
        
        if (line.find("Content-Length:") == 0) {
            contentLength = std::stoi(line.substr(15));
        }
    }
    
    if (contentLength == 0) {
        return "";
    }
    
    
    std::string content(contentLength, '\0');
    std::cin.read(&content[0], contentLength);
    
    return content;
}

void LspServer::sendMessage(const std::string& message) {
    log("Sending message, length=" + std::to_string(message.size()));
    std::string header = "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n";
    std::cout << header << message;
    std::cout.flush();
    log("Message sent");
}

void LspServer::log(const std::string& message) {
    std::cerr << "[quark-lsp] " << message << std::endl;
}

std::string LspServer::processMessage(const std::string& message) {
    try {
        std::string method = json::getString(message, "method");
        std::string id = json::getId(message);
        std::string params = json::getObject(message, "params");
        
        log("Processing: method=" + method + ", id=" + id);
        
        
        if (!id.empty()) {
            if (method == "initialize") {
                return handleInitialize(id, params);
            }
            if (method == "shutdown") {
                return handleShutdown(id);
            }
        if (method == "textDocument/completion") {
            return handleCompletion(id, params);
        }
        if (method == "textDocument/hover") {
            return handleHover(id, params);
        }
        if (method == "textDocument/definition") {
            return handleDefinition(id, params);
        }
        if (method == "textDocument/references") {
            return handleReferences(id, params);
        }
        if (method == "textDocument/documentSymbol") {
            return handleDocumentSymbol(id, params);
        }
        if (method == "textDocument/signatureHelp") {
            return handleSignatureHelp(id, params);
        }
        
        
        return createErrorResponse(id, -32601, "Method not found: " + method);
    }
    
    
    if (method == "initialized") {
        handleInitialized();
    }
    else if (method == "exit") {
        handleExit();
    }
    else if (method == "textDocument/didOpen") {
        handleDidOpen(params);
    }
    else if (method == "textDocument/didChange") {
        handleDidChange(params);
    }
    else if (method == "textDocument/didClose") {
        handleDidClose(params);
    }
    else if (method == "textDocument/didSave") {
        handleDidSave(params);
    }
    
    return "";
    } catch (const std::exception& e) {
        log("Exception in processMessage: " + std::string(e.what()));
        return "";
    } catch (...) {
        log("Unknown exception in processMessage");
        return "";
    }
}





std::string LspServer::createResponse(const std::string& id, const std::string& result) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}";
}

std::string LspServer::createErrorResponse(const std::string& id, int code, const std::string& message) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id + 
           ",\"error\":{\"code\":" + std::to_string(code) + 
           ",\"message\":\"" + json::escape(message) + "\"}}";
}

std::string LspServer::createNotification(const std::string& method, const std::string& params) {
    return "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"params\":" + params + "}";
}





std::string LspServer::handleInitialize(const std::string& id, const std::string& params) {
    log("Received initialize request, id=" + id);
    initialized_ = true;
    
    
    std::string capabilities = R"({
        "capabilities": {
            "textDocumentSync": {
                "openClose": true,
                "change": 1,
                "save": { "includeText": true }
            },
            "completionProvider": {
                "triggerCharacters": [".", ":", "<"],
                "resolveProvider": false
            },
            "hoverProvider": true,
            "signatureHelpProvider": {
                "triggerCharacters": ["(", ","]
            },
            "definitionProvider": true,
            "referencesProvider": true,
            "documentSymbolProvider": true
        },
        "serverInfo": {
            "name": "quark-lsp",
            "version": "1.0.0"
        }
    })";
    
    std::string response = createResponse(id, capabilities);
    log("Initialize response length: " + std::to_string(response.size()));
    return response;
}

std::string LspServer::handleInitialized() {
    log("Client initialized");
    return "";
}

std::string LspServer::handleShutdown(const std::string& id) {
    log("Received shutdown request");
    shutdownRequested_ = true;
    return createResponse(id, "null");
}

void LspServer::handleExit() {
    log("Received exit notification");
    std::exit(shutdownRequested_ ? 0 : 1);
}





void LspServer::handleDidOpen(const std::string& params) {
    log("Handling didOpen");
    std::string textDocument = json::getObject(params, "textDocument");
    std::string uri = json::getString(textDocument, "uri");
    std::string text = json::getString(textDocument, "text");
    int version = json::getInt(textDocument, "version");
    log("Opening document: " + uri);
    
    log("Document opened: " + uri);
    parseDocument(uri, text, version);
}

void LspServer::handleDidChange(const std::string& params) {
    log("Handling didChange");
    std::string textDocument = json::getObject(params, "textDocument");
    std::string uri = json::getString(textDocument, "uri");
    int version = json::getInt(textDocument, "version");
    log("Changing document: " + uri);
    
    
    
    size_t changesStart = params.find("\"contentChanges\"");
    if (changesStart != std::string::npos) {
        size_t textStart = params.find("\"text\"", changesStart);
        if (textStart != std::string::npos) {
            std::string text = json::getString(params.substr(textStart - 10), "text");
            parseDocument(uri, text, version);
        }
    }
}

void LspServer::handleDidClose(const std::string& params) {
    std::string textDocument = json::getObject(params, "textDocument");
    std::string uri = json::getString(textDocument, "uri");
    
    log("Document closed: " + uri);
    documents_.erase(uri);
}

void LspServer::handleDidSave(const std::string& params) {
    std::string textDocument = json::getObject(params, "textDocument");
    std::string uri = json::getString(textDocument, "uri");
    std::string text = json::getString(params, "text");
    
    if (!text.empty()) {
        log("Document saved: " + uri);
        parseDocument(uri, text, documents_[uri].version + 1);
    }
}

void LspServer::parseDocument(const std::string& uri, const std::string& content, int version) {
    log("Parsing document: " + uri + ", length=" + std::to_string(content.size()));
    if (content.size() > 0) {
        log("First 200 chars: " + content.substr(0, std::min<size_t>(200, content.size())));
    }
    
    ParsedDocument doc;
    doc.uri = uri;
    doc.content = content;
    doc.version = version;
    
    
    LspLexer lexer(content, uri);
    doc.tokens = lexer.tokenize();
    
    log("Lexed " + std::to_string(doc.tokens.size()) + " tokens");
    
    
    for (const auto& err : lexer.getErrors()) {
        Diagnostic diag;
        diag.range.start.line = err.range.startLine;
        diag.range.start.character = err.range.startColumn;
        diag.range.end.line = err.range.endLine;
        diag.range.end.character = err.range.endColumn;
        diag.severity = DiagnosticSeverity::Error;
        diag.message = err.message;
        diag.source = "quark";
        doc.diagnostics.push_back(diag);
    }
    
    log("Lexer produced " + std::to_string(doc.diagnostics.size()) + " errors");
    
    
    LspParser parser(doc.tokens, content);
    doc.ast = parser.parse();
    
    
    auto parserDiags = parser.getDiagnostics();
    
    log("Parser produced " + std::to_string(parserDiags.size()) + " errors");
    for (const auto& diag : parserDiags) {
        doc.diagnostics.push_back(diag);
    }
    
    
    documents_[uri] = std::move(doc);
    
    
    publishDiagnostics(uri, documents_[uri].diagnostics);
}

void LspServer::publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics) {
    std::ostringstream ss;
    ss << "{\"uri\":\"" << json::escape(uri) << "\",\"diagnostics\":[";
    
    for (size_t i = 0; i < diagnostics.size(); i++) {
        if (i > 0) ss << ",";
        const auto& d = diagnostics[i];
        ss << "{\"range\":{\"start\":{\"line\":" << d.range.start.line 
           << ",\"character\":" << d.range.start.character << "},"
           << "\"end\":{\"line\":" << d.range.end.line 
           << ",\"character\":" << d.range.end.character << "}},"
           << "\"severity\":" << static_cast<int>(d.severity) << ","
           << "\"source\":\"" << json::escape(d.source) << "\","
           << "\"message\":\"" << json::escape(d.message) << "\"}";
    }
    
    ss << "]}";
    
    sendMessage(createNotification("textDocument/publishDiagnostics", ss.str()));
}





std::string LspServer::handleCompletion(const std::string& id, const std::string& params) {
    log("Handling completion request");
    
    std::string textDocument = json::getObject(params, "textDocument");
    std::string uri = json::getString(textDocument, "uri");
    std::string position = json::getObject(params, "position");
    int line = json::getInt(position, "line");
    int character = json::getInt(position, "character");
    
    log("Completion request for " + uri + " at line " + std::to_string(line) + ", char " + std::to_string(character));
    
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        log("Document not found: " + uri);
        return createResponse(id, "{\"isIncomplete\":false,\"items\":[]}");
    }
    
    log("Document found, analyzing...");
    DocumentAnalyzer analyzer(it->second);
    Position pos{line, character};
    CompletionList completions = analyzer.getCompletions(pos);
    
    log("Found " + std::to_string(completions.items.size()) + " completions");
    
    
    std::ostringstream ss;
    ss << "{\"isIncomplete\":" << (completions.isIncomplete ? "true" : "false") << ",\"items\":[";
    
    for (size_t i = 0; i < completions.items.size(); i++) {
        if (i > 0) ss << ",";
        const auto& item = completions.items[i];
        ss << "{\"label\":\"" << json::escape(item.label) << "\""
           << ",\"kind\":" << static_cast<int>(item.kind);
        if (!item.detail.empty()) {
            ss << ",\"detail\":\"" << json::escape(item.detail) << "\"";
        }
        if (!item.documentation.empty()) {
            ss << ",\"documentation\":\"" << json::escape(item.documentation) << "\"";
        }
        if (!item.insertText.empty()) {
            ss << ",\"insertText\":\"" << json::escape(item.insertText) << "\"";
        }
        ss << "}";
    }
    
    ss << "]}";
    return createResponse(id, ss.str());
}

std::string LspServer::handleHover(const std::string& id, const std::string& params) {
    std::string textDocument = json::getObject(params, "textDocument");
    std::string uri = json::getString(textDocument, "uri");
    std::string position = json::getObject(params, "position");
    int line = json::getInt(position, "line");
    int character = json::getInt(position, "character");
    
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return createResponse(id, "null");
    }
    
    DocumentAnalyzer analyzer(it->second);
    Position pos{line, character};
    auto hover = analyzer.getHover(pos);
    
    if (!hover) {
        return createResponse(id, "null");
    }
    
    std::ostringstream ss;
    ss << "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" 
       << json::escape(hover->contents) << "\"}";
    if (hover->range) {
        ss << ",\"range\":{\"start\":{\"line\":" << hover->range->start.line 
           << ",\"character\":" << hover->range->start.character << "},"
           << "\"end\":{\"line\":" << hover->range->end.line 
           << ",\"character\":" << hover->range->end.character << "}}";
    }
    ss << "}";
    
    return createResponse(id, ss.str());
}

std::string LspServer::handleDefinition(const std::string& id, const std::string& params) {
    std::string textDocument = json::getObject(params, "textDocument");
    std::string uri = json::getString(textDocument, "uri");
    std::string position = json::getObject(params, "position");
    int line = json::getInt(position, "line");
    int character = json::getInt(position, "character");
    
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return createResponse(id, "null");
    }
    
    DocumentAnalyzer analyzer(it->second);
    Position pos{line, character};
    auto location = analyzer.findDefinition(pos);
    
    if (!location) {
        return createResponse(id, "null");
    }
    
    std::ostringstream ss;
    ss << "{\"uri\":\"" << json::escape(location->uri) << "\","
       << "\"range\":{\"start\":{\"line\":" << location->range.start.line 
       << ",\"character\":" << location->range.start.character << "},"
       << "\"end\":{\"line\":" << location->range.end.line 
       << ",\"character\":" << location->range.end.character << "}}}";
    
    return createResponse(id, ss.str());
}

std::string LspServer::handleReferences(const std::string& id, const std::string& params) {
    std::string textDocument = json::getObject(params, "textDocument");
    std::string uri = json::getString(textDocument, "uri");
    std::string position = json::getObject(params, "position");
    int line = json::getInt(position, "line");
    int character = json::getInt(position, "character");
    
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return createResponse(id, "[]");
    }
    
    DocumentAnalyzer analyzer(it->second);
    Position pos{line, character};
    auto references = analyzer.findReferences(pos);
    
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < references.size(); i++) {
        if (i > 0) ss << ",";
        const auto& loc = references[i];
        ss << "{\"uri\":\"" << json::escape(loc.uri) << "\","
           << "\"range\":{\"start\":{\"line\":" << loc.range.start.line 
           << ",\"character\":" << loc.range.start.character << "},"
           << "\"end\":{\"line\":" << loc.range.end.line 
           << ",\"character\":" << loc.range.end.character << "}}}";
    }
    ss << "]";
    
    return createResponse(id, ss.str());
}

std::string LspServer::handleDocumentSymbol(const std::string& id, const std::string& params) {
    std::string textDocument = json::getObject(params, "textDocument");
    std::string uri = json::getString(textDocument, "uri");
    
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return createResponse(id, "[]");
    }
    
    DocumentAnalyzer analyzer(it->second);
    auto symbols = analyzer.getDocumentSymbols();
    
    std::function<void(std::ostringstream&, const std::vector<DocumentSymbol>&)> writeSymbols;
    writeSymbols = [&](std::ostringstream& ss, const std::vector<DocumentSymbol>& syms) {
        ss << "[";
        for (size_t i = 0; i < syms.size(); i++) {
            if (i > 0) ss << ",";
            const auto& sym = syms[i];
            ss << "{\"name\":\"" << json::escape(sym.name) << "\""
               << ",\"kind\":" << static_cast<int>(sym.kind)
               << ",\"range\":{\"start\":{\"line\":" << sym.range.start.line 
               << ",\"character\":" << sym.range.start.character << "},"
               << "\"end\":{\"line\":" << sym.range.end.line 
               << ",\"character\":" << sym.range.end.character << "}}"
               << ",\"selectionRange\":{\"start\":{\"line\":" << sym.selectionRange.start.line 
               << ",\"character\":" << sym.selectionRange.start.character << "},"
               << "\"end\":{\"line\":" << sym.selectionRange.end.line 
               << ",\"character\":" << sym.selectionRange.end.character << "}}";
            if (!sym.detail.empty()) {
                ss << ",\"detail\":\"" << json::escape(sym.detail) << "\"";
            }
            if (!sym.children.empty()) {
                ss << ",\"children\":";
                writeSymbols(ss, sym.children);
            }
            ss << "}";
        }
        ss << "]";
    };
    
    std::ostringstream ss;
    writeSymbols(ss, symbols);
    return createResponse(id, ss.str());
}

std::string LspServer::handleSignatureHelp(const std::string& id, const std::string& params) {
    std::string textDocument = json::getObject(params, "textDocument");
    std::string uri = json::getString(textDocument, "uri");
    std::string position = json::getObject(params, "position");
    int line = json::getInt(position, "line");
    int character = json::getInt(position, "character");
    
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return createResponse(id, "null");
    }
    
    DocumentAnalyzer analyzer(it->second);
    Position pos{line, character};
    auto help = analyzer.getSignatureHelp(pos);
    
    if (!help) {
        return createResponse(id, "null");
    }
    
    std::ostringstream ss;
    ss << "{\"signatures\":[";
    for (size_t i = 0; i < help->signatures.size(); i++) {
        if (i > 0) ss << ",";
        const auto& sig = help->signatures[i];
        ss << "{\"label\":\"" << json::escape(sig.label) << "\"";
        if (!sig.documentation.empty()) {
            ss << ",\"documentation\":\"" << json::escape(sig.documentation) << "\"";
        }
        ss << ",\"parameters\":[";
        for (size_t j = 0; j < sig.parameters.size(); j++) {
            if (j > 0) ss << ",";
            const auto& param = sig.parameters[j];
            ss << "{\"label\":\"" << json::escape(param.label) << "\"";
            if (!param.documentation.empty()) {
                ss << ",\"documentation\":\"" << json::escape(param.documentation) << "\"";
            }
            ss << "}";
        }
        ss << "]}";
    }
    ss << "],\"activeSignature\":" << help->activeSignature 
       << ",\"activeParameter\":" << help->activeParameter << "}";
    
    return createResponse(id, ss.str());
}





DocumentAnalyzer::DocumentAnalyzer(const ParsedDocument& doc) : doc_(doc) {
    buildSymbolTable();
}

void DocumentAnalyzer::buildSymbolTable() {
    if (!doc_.ast) return;
    
    for (const auto& stmt : doc_.ast->statements) {
        visitStmt(stmt.get());
    }
}

void DocumentAnalyzer::visitStmt(const Stmt* stmt) {
    if (!stmt) return;
    
    if (auto* func = dynamic_cast<const FunctionDef*>(stmt)) {
        SymbolDefinition def;
        def.name = func->name;
        def.kind = SymbolKind::Function;
        def.location.uri = doc_.uri;
        def.location.range = toLspRange(func->range);
        symbolTable_[func->name] = def;
        
        
        for (const auto& s : func->body) {
            visitStmt(s.get());
        }
    }
    else if (auto* structDef = dynamic_cast<const StructDef*>(stmt)) {
        SymbolDefinition def;
        def.name = structDef->name;
        def.kind = SymbolKind::Struct;
        def.location.uri = doc_.uri;
        def.location.range = toLspRange(structDef->range);
        symbolTable_[structDef->name] = def;
        
        
        for (const auto& field : structDef->fields) {
            SymbolDefinition fieldDef;
            fieldDef.name = structDef->name + "." + field.name;
            fieldDef.kind = SymbolKind::Field;
            fieldDef.location.uri = doc_.uri;
            fieldDef.location.range = toLspRange(field.range);
            symbolTable_[fieldDef.name] = fieldDef;
        }
        
        
        for (const auto& method : structDef->methods) {
            SymbolDefinition methodDef;
            methodDef.name = structDef->name + "." + method.name;
            methodDef.kind = SymbolKind::Method;
            methodDef.location.uri = doc_.uri;
            methodDef.location.range = toLspRange(method.range);
            symbolTable_[methodDef.name] = methodDef;
        }
    }
    else if (auto* varDecl = dynamic_cast<const VarDecl*>(stmt)) {
        SymbolDefinition def;
        def.name = varDecl->name;
        def.kind = SymbolKind::Variable;
        def.location.uri = doc_.uri;
        def.location.range = toLspRange(varDecl->range);
        symbolTable_[varDecl->name] = def;
    }
    else if (auto* impl = dynamic_cast<const ImplBlock*>(stmt)) {
        for (const auto& method : impl->methods) {
            SymbolDefinition methodDef;
            methodDef.name = impl->structName + "." + method.name;
            methodDef.kind = SymbolKind::Method;
            methodDef.location.uri = doc_.uri;
            methodDef.location.range = toLspRange(method.range);
            symbolTable_[methodDef.name] = methodDef;
        }
    }
    else if (auto* ifStmt = dynamic_cast<const IfStmt*>(stmt)) {
        for (const auto& s : ifStmt->thenBody) {
            visitStmt(s.get());
        }
        for (const auto& elif : ifStmt->elifs) {
            for (const auto& s : elif.second) {
                visitStmt(s.get());
            }
        }
        for (const auto& s : ifStmt->elseBody) {
            visitStmt(s.get());
        }
    }
    else if (auto* whileStmt = dynamic_cast<const WhileStmt*>(stmt)) {
        for (const auto& s : whileStmt->body) {
            visitStmt(s.get());
        }
    }
    else if (auto* forStmt = dynamic_cast<const ForStmt*>(stmt)) {
        for (const auto& s : forStmt->body) {
            visitStmt(s.get());
        }
    }
}

Range DocumentAnalyzer::toLspRange(const SourceRange& range) {
    Range r;
    r.start.line = range.startLine;
    r.start.character = range.startColumn;
    r.end.line = range.endLine;
    r.end.character = range.endColumn;
    return r;
}

bool DocumentAnalyzer::positionInRange(const Position& pos, const SourceRange& range) {
    if (pos.line < range.startLine || pos.line > range.endLine) {
        return false;
    }
    if (pos.line == range.startLine && pos.character < range.startColumn) {
        return false;
    }
    if (pos.line == range.endLine && pos.character > range.endColumn) {
        return false;
    }
    return true;
}

std::string DocumentAnalyzer::getIdentifierAt(const Position& pos) {
    
    for (const auto& token : doc_.tokens) {
        if (token.kind == TokenKind::Identifier && 
            positionInRange(pos, token.range)) {
            return token.text;
        }
    }
    return "";
}

std::vector<DocumentSymbol> DocumentAnalyzer::getDocumentSymbols() {
    std::vector<DocumentSymbol> symbols;
    if (!doc_.ast) return symbols;
    
    for (const auto& stmt : doc_.ast->statements) {
        if (auto* func = dynamic_cast<const FunctionDef*>(stmt.get())) {
            DocumentSymbol sym;
            sym.name = func->name;
            sym.kind = SymbolKind::Function;
            sym.range = toLspRange(func->range);
            sym.selectionRange = sym.range;
            symbols.push_back(sym);
        }
        else if (auto* structDef = dynamic_cast<const StructDef*>(stmt.get())) {
            DocumentSymbol sym;
            sym.name = structDef->name;
            sym.kind = SymbolKind::Struct;
            sym.range = toLspRange(structDef->range);
            sym.selectionRange = sym.range;
            
            
            for (const auto& field : structDef->fields) {
                DocumentSymbol fieldSym;
                fieldSym.name = field.name;
                fieldSym.kind = SymbolKind::Field;
                fieldSym.range = toLspRange(field.range);
                fieldSym.selectionRange = fieldSym.range;
                sym.children.push_back(fieldSym);
            }
            
            
            for (const auto& method : structDef->methods) {
                DocumentSymbol methodSym;
                methodSym.name = method.name;
                methodSym.kind = SymbolKind::Method;
                methodSym.range = toLspRange(method.range);
                methodSym.selectionRange = methodSym.range;
                sym.children.push_back(methodSym);
            }
            
            symbols.push_back(sym);
        }
        else if (auto* varDecl = dynamic_cast<const VarDecl*>(stmt.get())) {
            DocumentSymbol sym;
            sym.name = varDecl->name;
            sym.kind = SymbolKind::Variable;
            sym.range = toLspRange(varDecl->range);
            sym.selectionRange = sym.range;
            symbols.push_back(sym);
        }
        else if (auto* impl = dynamic_cast<const ImplBlock*>(stmt.get())) {
            DocumentSymbol sym;
            sym.name = impl->structName + " impl";
            sym.kind = SymbolKind::Namespace;
            sym.range = toLspRange(impl->range);
            sym.selectionRange = sym.range;
            
            for (const auto& method : impl->methods) {
                DocumentSymbol methodSym;
                methodSym.name = method.name;
                methodSym.kind = SymbolKind::Method;
                methodSym.range = toLspRange(method.range);
                methodSym.selectionRange = methodSym.range;
                sym.children.push_back(methodSym);
            }
            
            symbols.push_back(sym);
        }
        else if (auto* mod = dynamic_cast<const ModuleDecl*>(stmt.get())) {
            DocumentSymbol sym;
            sym.name = mod->name;
            sym.kind = SymbolKind::Module;
            sym.range = toLspRange(mod->range);
            sym.selectionRange = sym.range;
            symbols.push_back(sym);
        }
    }
    
    return symbols;
}

std::optional<Location> DocumentAnalyzer::findDefinition(const Position& pos) {
    std::string name = getIdentifierAt(pos);
    if (name.empty()) {
        return std::nullopt;
    }
    
    auto it = symbolTable_.find(name);
    if (it != symbolTable_.end()) {
        return it->second.location;
    }
    
    return std::nullopt;
}

std::vector<Location> DocumentAnalyzer::findReferences(const Position& pos) {
    std::vector<Location> refs;
    std::string name = getIdentifierAt(pos);
    if (name.empty()) {
        return refs;
    }
    
    
    for (const auto& token : doc_.tokens) {
        if (token.kind == TokenKind::Identifier && token.text == name) {
            Location loc;
            loc.uri = doc_.uri;
            loc.range = toLspRange(token.range);
            refs.push_back(loc);
        }
    }
    
    return refs;
}

std::optional<Hover> DocumentAnalyzer::getHover(const Position& pos) {
    std::string name = getIdentifierAt(pos);
    if (name.empty()) {
        return std::nullopt;
    }
    
    
    auto it = symbolTable_.find(name);
    if (it != symbolTable_.end()) {
        Hover hover;
        std::string kindName;
        switch (it->second.kind) {
            case SymbolKind::Function: kindName = "function"; break;
            case SymbolKind::Struct: kindName = "struct"; break;
            case SymbolKind::Variable: kindName = "variable"; break;
            case SymbolKind::Field: kindName = "field"; break;
            case SymbolKind::Method: kindName = "method"; break;
            default: kindName = "symbol"; break;
        }
        hover.contents = "**" + name + "** (" + kindName + ")";
        if (!it->second.detail.empty()) {
            hover.contents += "\n\n" + it->second.detail;
        }
        return hover;
    }
    
    
    for (const auto& token : doc_.tokens) {
        if (positionInRange(pos, token.range) && token.isKeyword()) {
            Hover hover;
            hover.contents = "**" + token.text + "** (keyword)";
            return hover;
        }
    }
    
    return std::nullopt;
}

CompletionList DocumentAnalyzer::getCompletions(const Position& pos) {
    CompletionList list;
    
    
    addKeywordCompletions(list);
    
    
    addTypeCompletions(list);
    
    
    for (const auto& [name, def] : symbolTable_) {
        CompletionItem item;
        item.label = name;
        switch (def.kind) {
            case SymbolKind::Function:
                item.kind = CompletionItemKind::Function;
                break;
            case SymbolKind::Struct:
                item.kind = CompletionItemKind::Struct;
                break;
            case SymbolKind::Variable:
                item.kind = CompletionItemKind::Variable;
                break;
            case SymbolKind::Field:
                item.kind = CompletionItemKind::Field;
                break;
            case SymbolKind::Method:
                item.kind = CompletionItemKind::Method;
                break;
            default:
                item.kind = CompletionItemKind::Text;
                break;
        }
        item.detail = def.detail;
        list.items.push_back(item);
    }
    
    
    addBuiltinCompletions(list);
    
    return list;
}

void DocumentAnalyzer::addKeywordCompletions(CompletionList& list) {
    static const std::vector<std::string> keywords = {
        "module", "import", "extern", "struct", "impl", "data", "extend",
        "fn", "var", "if", "elif", "else", "while", "for", "in",
        "match", "ret", "break", "continue", "this", "true", "false", "null"
    };
    
    for (const auto& kw : keywords) {
        CompletionItem item;
        item.label = kw;
        item.kind = CompletionItemKind::Keyword;
        list.items.push_back(item);
    }
}

void DocumentAnalyzer::addTypeCompletions(CompletionList& list) {
    static const std::vector<std::string> types = {
        "int", "float", "double", "bool", "str", "char", "void", "map", "list"
    };
    
    for (const auto& t : types) {
        CompletionItem item;
        item.label = t;
        item.kind = CompletionItemKind::Keyword;
        item.detail = "type";
        list.items.push_back(item);
    }
}

void DocumentAnalyzer::addBuiltinCompletions(CompletionList& list) {
    static const std::vector<std::pair<std::string, std::string>> builtins = {
        
        {"print", "void print(...)"},
        {"readline", "str readline(...)"},
        {"format", "str format(...)"},
        
        
        {"to_string", "str to_string(...)"},
        {"toString", "str toString(...)"},
        {"to_int", "int to_int(...)"},
        {"parse_int", "int parse_int(...)"},
        {"parseInt", "int parseInt(...)"},
        
        
        {"str_concat", "str str_concat(a: str, b: str)"},
        {"str_slice", "str str_slice(s: str, start: int, end: int)"},
        {"str_find", "bool str_find(haystack: str, needle: str)"},
        {"str_replace", "str str_replace(s: str, old: str, new: str)"},
        {"str_split", "str[] str_split(s: str, delim: str)"},
        {"str_len", "int str_len(s: str)"},
        {"str_length", "int str_length(s: str)"},
        {"str_starts_with", "bool str_starts_with(s: str, prefix: str)"},
        {"str_ends_with", "bool str_ends_with(s: str, suffix: str)"},
        
        
        {"sin", "double sin(x: double)"},
        {"cos", "double cos(x: double)"},
        {"tan", "double tan(x: double)"},
        {"asin", "double asin(x: double)"},
        {"acos", "double acos(x: double)"},
        {"atan", "double atan(x: double)"},
        {"atan2", "double atan2(y: double, x: double)"},
        {"sinh", "double sinh(x: double)"},
        {"cosh", "double cosh(x: double)"},
        {"tanh", "double tanh(x: double)"},
        
        
        {"sqrt", "double sqrt(x: double)"},
        {"pow", "double pow(base: double, exp: double)"},
        {"log", "double log(x: double)"},
        {"log10", "double log10(x: double)"},
        {"exp", "double exp(x: double)"},
        {"abs", "double abs(x: double)"},
        {"floor", "double floor(x: double)"},
        {"ceil", "double ceil(x: double)"},
        {"round", "double round(x: double)"},
        {"fmod", "double fmod(x: double, y: double)"},
        
        
        {"abs_i32", "int abs_i32(x: int)"},
        {"abs_f64", "double abs_f64(x: double)"},
        {"min_i32", "int min_i32(a: int, b: int)"},
        {"max_i32", "int max_i32(a: int, b: int)"},
        {"min_f64", "double min_f64(a: double, b: double)"},
        {"max_f64", "double max_f64(a: double, b: double)"},
        {"min", "double min(...)"},
        {"max", "double max(...)"},
        {"clamp_i32", "int clamp_i32(x: int, lo: int, hi: int)"},
        {"clamp_f64", "double clamp_f64(x: double, lo: double, hi: double)"},
        {"clamp", "double clamp(...)"},
        
        
        {"alloc", "void* alloc(size: int)"},
        {"free", "void free(ptr: void*)"},
        {"realloc", "void* realloc(ptr: void*, new_size: int)"},
        {"memset", "void* memset(ptr: void*, value: int, size: int)"},
        {"memcpy", "void* memcpy(dest: void*, src: void*, size: int)"},
        
        
        {"sleep", "void sleep(ms: int)"},
    };
    
    for (const auto& [name, sig] : builtins) {
        CompletionItem item;
        item.label = name;
        item.kind = CompletionItemKind::Function;
        item.detail = sig;
        item.insertText = name + "(";
        list.items.push_back(item);
    }
}

std::optional<SignatureHelp> DocumentAnalyzer::getSignatureHelp(const Position& pos) {
    
    
    
    
    int parenDepth = 0;
    int argIndex = 0;
    std::string funcName;
    
    for (auto it = doc_.tokens.rbegin(); it != doc_.tokens.rend(); ++it) {
        const auto& token = *it;
        
        
        if (token.range.startLine > pos.line ||
            (token.range.startLine == pos.line && token.range.startColumn > pos.character)) {
            continue;
        }
        
        if (token.kind == TokenKind::RightParen) {
            parenDepth++;
        }
        else if (token.kind == TokenKind::LeftParen) {
            if (parenDepth == 0) {
                
                auto prev = it + 1;
                if (prev != doc_.tokens.rend() && prev->kind == TokenKind::Identifier) {
                    funcName = prev->text;
                    break;
                }
            }
            parenDepth--;
        }
        else if (token.kind == TokenKind::Comma && parenDepth == 0) {
            argIndex++;
        }
    }
    
    if (funcName.empty()) {
        return std::nullopt;
    }
    
    
    auto it = symbolTable_.find(funcName);
    if (it == symbolTable_.end() || it->second.kind != SymbolKind::Function) {
        return std::nullopt;
    }
    
    SignatureHelp help;
    SignatureInformation sig;
    sig.label = funcName + "(...)";  
    sig.documentation = it->second.detail;
    help.signatures.push_back(sig);
    help.activeSignature = 0;
    help.activeParameter = argIndex;
    
    return help;
}

} 
