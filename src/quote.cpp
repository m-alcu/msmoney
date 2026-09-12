#include "quote.h"
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// keep only what's safe to drop into a single-quoted shell/URL argument
std::string sanitize(const std::string& raw, const char* extra) {
    std::string out;
    for (char c : raw) {
        if (std::isalnum((unsigned char)c) || strchr(extra, c))
            out += (char)std::toupper((unsigned char)c);
    }
    return out;
}

std::string runCommand(const std::string& cmd) {
    std::array<char, 4096> buf{};
    std::string result;
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "";
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe.get())) > 0) result.append(buf.data(), n);
    return result;
}

// pulls the first "key":"value" or "key":123.45 occurrence out of a JSON blob;
// good enough for the flat fields we need without pulling in a JSON library
std::optional<std::string> extractField(const std::string& json, const std::string& key) {
    std::string marker = "\"" + key + "\":";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return std::nullopt;
    pos += marker.size();
    if (pos >= json.size()) return std::nullopt;
    if (json[pos] == '"') {
        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return std::nullopt;
        return json.substr(pos + 1, end - pos - 1);
    }
    size_t end = pos;
    while (end < json.size() &&
          (std::isdigit((unsigned char)json[end]) || json[end] == '.' || json[end] == '-'))
        end++;
    if (end == pos) return std::nullopt;
    return json.substr(pos, end - pos);
}

// pulls every "key":"value" occurrence out of a JSON blob, in the order they
// appear; used to walk Yahoo's ranked list of search candidates
std::vector<std::string> extractAllFields(const std::string& json, const std::string& key) {
    std::vector<std::string> out;
    std::string marker = "\"" + key + "\":\"";
    size_t pos = 0;
    while ((pos = json.find(marker, pos)) != std::string::npos) {
        pos += marker.size();
        size_t end = json.find('"', pos);
        if (end == std::string::npos) break;
        out.push_back(json.substr(pos, end - pos));
        pos = end;
    }
    return out;
}

// true if every character is safe to embed in a single-quoted shell argument
// and is a plausible URL character; rejects anything that could break out of
// the quoting (', backtick, $, ;, |, &, whitespace, ...)
bool isSafeUrl(const std::string& url) {
    static const char* allowed = ":/.-_~%?=&,+#@";
    for (char c : url)
        if (!std::isalnum((unsigned char)c) && !strchr(allowed, c)) return false;
    return true;
}

}  // namespace

std::optional<double> fetchPriceByIsin(const std::string& rawIsin, std::string* error,
                                       std::string* currency) {
    // the ISIN field also doubles as a plain Yahoo ticker (e.g. "IBE.MC"),
    // so keep the punctuation such symbols use instead of stripping it down
    // to alphanumerics only, which would silently mangle the search query
    std::string isin = sanitize(rawIsin, ".-^=");
    if (isin.empty()) {
        if (error) *error = "No ISIN set for this asset";
        return std::nullopt;
    }

    // step 1: resolve the ISIN to a ranked list of candidate trading symbols.
    // A real 12-character ISIN normally resolves to a single, unambiguous
    // symbol; a bare ticker (or a short/ambiguous code) can fuzzy-match
    // several, so ask for a handful and let step 2 pick the best one.
    std::string searchJson = runCommand(
        "curl -s --max-time 8 -A 'Mozilla/5.0' "
        "'https://query2.finance.yahoo.com/v1/finance/search?q=" + isin +
        "&quotesCount=8&newsCount=0'");
    auto symbols = extractAllFields(searchJson, "symbol");
    if (symbols.empty()) {
        if (error) *error = "No symbol found online for ISIN " + isin;
        return std::nullopt;
    }

    // step 2: read each candidate's latest quote, in ranked order, and take
    // the first one priced in EUR - this app has no multi-currency support,
    // so a same-named USD listing (an ADR, or an unrelated US ticker) must
    // not win just because Yahoo ranked it first. Fall back to the top
    // candidate if none are in EUR.
    std::optional<double> fallbackPrice;
    std::string fallbackCurrency, fallbackSymbol;
    for (size_t i = 0; i < symbols.size() && i < 6; i++) {
        std::string sym = sanitize(symbols[i], ".-^=");
        if (sym.empty()) continue;
        std::string quoteJson = runCommand(
            "curl -s --max-time 8 -A 'Mozilla/5.0' "
            "'https://query1.finance.yahoo.com/v8/finance/chart/" + sym + "'");
        auto priceStr = extractField(quoteJson, "regularMarketPrice");
        if (!priceStr) continue;
        double price;
        try {
            price = std::stod(*priceStr);
        } catch (...) {
            continue;
        }
        std::string cur = extractField(quoteJson, "currency").value_or("");
        if (!fallbackPrice) {
            fallbackPrice = price;
            fallbackCurrency = cur;
            fallbackSymbol = sym;
        }
        if (cur == "EUR") {
            if (currency) *currency = cur;
            return price;
        }
    }
    if (fallbackPrice) {
        if (currency) *currency = fallbackCurrency;
        return fallbackPrice;
    }
    if (error) *error = "No price found online for ISIN " + isin;
    return std::nullopt;
}

std::optional<double> fetchPriceFromFinect(const std::string& url, std::string* error,
                                           std::string* currency) {
    if (url.empty()) {
        if (error) *error = "No Finect URL set for this asset";
        return std::nullopt;
    }
    if (url.rfind("https://www.finect.com/", 0) != 0) {
        if (error) *error = "Finect URL must start with https://www.finect.com/";
        return std::nullopt;
    }
    if (!isSafeUrl(url)) {
        if (error) *error = "Invalid character in Finect URL";
        return std::nullopt;
    }

    // the fund/stock page embeds a schema.org JSON-LD block with its current
    // price and currency, e.g. "offers":{"price":"14.05","priceCurrency":"EUR"}
    std::string html = runCommand("curl -sL --max-time 10 -A 'Mozilla/5.0' '" + url + "'");
    auto priceStr = extractField(html, "price");
    if (!priceStr) {
        if (error) *error = "No price found on the Finect page";
        return std::nullopt;
    }
    if (currency) currency->clear();
    if (auto cur = extractField(html, "priceCurrency"); cur && currency) *currency = *cur;
    try {
        return std::stod(*priceStr);
    } catch (...) {
        if (error) *error = "Malformed price data on the Finect page";
        return std::nullopt;
    }
}
