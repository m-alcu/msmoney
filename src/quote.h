#pragma once
#include <optional>
#include <string>

// Best-effort online price lookup by ISIN. Resolves the ISIN to a trading
// symbol and reads back its latest quote using Yahoo Finance's public but
// unofficial, unauthenticated endpoints (via the system `curl` binary, so no
// extra library dependency). Returns nullopt and fills *error with a
// human-readable reason on any failure: no network, no match for the ISIN,
// endpoint shape changed, etc. This is a manual, on-demand lookup - nothing
// calls it automatically, and there's no guarantee Yahoo keeps this stable.
std::optional<double> fetchPriceByIsin(const std::string& isin, std::string* error);

// Reads the price out of a finect.com fund/stock page (its embedded
// schema.org JSON-LD block, e.g. "offers":{"price":"14.05","priceCurrency":
// "EUR"}). Preferred over fetchPriceByIsin when a Finect URL is known, since
// it reports the price in the fund's own trading currency - Yahoo's ISIN
// search can resolve to a US listing and answer in USD even for a EUR fund.
// Only https://www.finect.com/... URLs are accepted. currency, if given, is
// set to the page's reported currency code (e.g. "EUR") on success.
std::optional<double> fetchPriceFromFinect(const std::string& url, std::string* error,
                                           std::string* currency = nullptr);
