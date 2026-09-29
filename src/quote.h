#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Best-effort online price lookup by ISIN. Resolves the ISIN to a trading
// symbol and reads back its latest quote using Yahoo Finance's public but
// unofficial, unauthenticated endpoints (via the system `curl` binary, so no
// extra library dependency). Yahoo's search is a fuzzy match and can rank an
// unrelated, similarly-named ticker above the actual security (e.g. "IBE"
// matching US-listed "IBEX" before Madrid-listed "IBE.MC"), so this scans
// the ranked candidates and prefers the first one quoted in EUR - this app
// has no multi-currency support. currency, if given, is set to the chosen
// candidate's reported currency code (e.g. "EUR") on success. asOf, if
// given, is set to the quote's own timestamp (YYYY-MM-DD, local time) when
// Yahoo reports one, left untouched otherwise. Returns nullopt and fills
// *error with a human-readable reason on any failure: no network, no match
// for the ISIN, endpoint shape changed, etc. This is a manual, on-demand
// lookup - nothing calls it automatically, and there's no guarantee Yahoo
// keeps this stable.
std::optional<double> fetchPriceByIsin(const std::string& isin, std::string* error,
                                       std::string* currency = nullptr,
                                       std::string* asOf = nullptr);

// Reads the price out of a finect.com fund/stock page (its embedded
// schema.org JSON-LD block, e.g. "offers":{"price":"14.05","priceCurrency":
// "EUR"}). Preferred over fetchPriceByIsin when a Finect URL is known, since
// it reports the price in the fund's own trading currency - Yahoo's ISIN
// search can resolve to a US listing and answer in USD even for a EUR fund.
// Only https://www.finect.com/... URLs are accepted. currency, if given, is
// set to the page's reported currency code (e.g. "EUR") on success. asOf, if
// given, is set to the fund's own "last NAV update" date (YYYY-MM-DD) - what
// Finect labels "Fecha de actualización del valor liquidativo" - read out of
// the page's embedded state; left untouched if that field isn't found.
std::optional<double> fetchPriceFromFinect(const std::string& url, std::string* error,
                                           std::string* currency = nullptr,
                                           std::string* asOf = nullptr);

// Downloads a finect.com fund/ETF/pension-plan page's full daily NAV
// history: the same "products/collectives/<type>/<id>/timeseries" endpoint
// the page's own chart calls, read via Finect's public API host using the
// front-end API key baked into their own JS bundle (not a secret - it's
// shipped to every browser). The page is fetched once to read its internal
// product id (the schema.org "sku" field) and its category is read off the
// URL itself (e.g. "planes-pensiones" -> "plans"); only fund/ETF/pension-plan/
// SICAV categories expose this endpoint. Returns chronological
// (YYYY-MM-DD, price) pairs from `start` to the latest published NAV.
// Returns nullopt and fills *error with a human-readable reason on failure.
std::optional<std::vector<std::pair<std::string, double>>> fetchHistoryFromFinect(
    const std::string& url, const std::string& start, std::string* error);
