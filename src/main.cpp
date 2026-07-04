#include "Swap.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace irs;

namespace {

struct MarketScenario {
	std::vector<DepositQuote> deposits;
	std::vector<SwapQuote> swaps;
	std::string sourceLabel{"Public India government bond curve snapshot"};
	std::string sourceDate{"4 Jul 2026"};
	double notional{100000000.0};
	double fixedCoupon{0.0400};
	double maturity{5.0};
	double paymentInterval{1.0};
	InterestRateSwap::Side side{InterestRateSwap::Side::PayFixed};
	double spread{0.0};
	double startTime{0.0};
};

std::string trim(const std::string& text) {
	const auto begin = std::find_if_not(text.begin(), text.end(), [](unsigned char ch) {
		return std::isspace(ch) != 0;
	});
	const auto end = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
		return std::isspace(ch) != 0;
	}).base();

	if (begin >= end) {
		return {};
	}
	return std::string(begin, end);
}

std::vector<std::string> splitCsvLine(const std::string& line) {
	std::vector<std::string> fields;
	std::string field;
	std::istringstream stream(line);
	while (std::getline(stream, field, ',')) {
		fields.push_back(trim(field));
	}

	if (!line.empty() && line.back() == ',') {
		fields.emplace_back();
	}

	return fields;
}

bool isCommentOrBlank(const std::string& line) {
	const std::string stripped = trim(line);
	return stripped.empty() || stripped.front() == '#';
}

InterestRateSwap::Side parseSide(const std::string& text) {
	const std::string lowered = [&]() {
		std::string value = trim(text);
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return value;
	}();

	if (lowered == "receivefixed" || lowered == "receive" || lowered == "recv") {
		return InterestRateSwap::Side::ReceiveFixed;
	}
	return InterestRateSwap::Side::PayFixed;
}

MarketScenario loadMarketQuotesCsv(const std::string& path) {
	std::ifstream input(path);
	if (!input) {
		throw std::runtime_error("Unable to open market quotes CSV: " + path);
	}

	MarketScenario scenario;
	std::string line;
	bool headerSkipped = false;

	while (std::getline(input, line)) {
		if (isCommentOrBlank(line)) {
			continue;
		}

		if (!headerSkipped) {
			headerSkipped = true;
			continue;
		}

		const auto fields = splitCsvLine(line);
		if (fields.size() < 4) {
			continue;
		}

		const std::string type = fields[0];
		const double maturity = std::stod(fields[1]);
		const double rate = std::stod(fields[2]);
		const double yearFraction = fields[3].empty() ? 1.0 : std::stod(fields[3]);
		const double paymentInterval = fields.size() > 4 && !fields[4].empty() ? std::stod(fields[4]) : 1.0;

		std::string loweredType = type;
		std::transform(loweredType.begin(), loweredType.end(), loweredType.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});

		if (loweredType == "deposit") {
			scenario.deposits.push_back({maturity, rate, yearFraction});
		} else if (loweredType == "swap") {
			scenario.swaps.push_back({maturity, rate, paymentInterval});
		}
	}

	if (scenario.deposits.empty() || scenario.swaps.empty()) {
		throw std::runtime_error("Market CSV must contain at least one deposit and one swap quote");
	}

	return scenario;
}

void applyTradeCsv(const std::string& path, MarketScenario& scenario) {
	std::ifstream input(path);
	if (!input) {
		throw std::runtime_error("Unable to open trade CSV: " + path);
	}

	std::string line;
	bool headerSkipped = false;
	while (std::getline(input, line)) {
		if (isCommentOrBlank(line)) {
			continue;
		}

		if (!headerSkipped) {
			headerSkipped = true;
			continue;
		}

		const auto fields = splitCsvLine(line);
		if (fields.size() < 6) {
			continue;
		}

		scenario.notional = std::stod(fields[0]);
		scenario.fixedCoupon = std::stod(fields[1]);
		scenario.maturity = std::stod(fields[2]);
		scenario.paymentInterval = std::stod(fields[3]);
		scenario.side = parseSide(fields[4]);
		scenario.spread = fields[5].empty() ? 0.0 : std::stod(fields[5]);
		scenario.startTime = fields.size() > 6 && !fields[6].empty() ? std::stod(fields[6]) : 0.0;
		return;
	}

	throw std::runtime_error("Trade CSV must contain one trade row");
}

void printCurveGrid(const YieldCurve& curve) {
	std::cout << "\nInterpolated curve snapshot\n";
	std::cout << std::setw(8) << "t"
			  << std::setw(16) << "zero rate"
			  << std::setw(16) << "discount factor" << '\n';

	for (double t : TimeGrid::regular(0.5, 10.0, 0.5)) {
		std::cout << std::setw(8) << std::fixed << std::setprecision(2) << t
				  << std::setw(16) << std::setprecision(6) << curve.zeroRate(t)
				  << std::setw(16) << curve.discountFactor(t) << '\n';
	}
}

void printCashFlowTable(const std::string& title, const std::vector<CashFlow>& cashFlows) {
	std::cout << "\n" << title << '\n';
	std::cout << std::setw(10) << "Start"
			  << std::setw(10) << "End"
			  << std::setw(12) << "Accrual"
			  << std::setw(14) << "Rate"
			  << std::setw(16) << "Amount"
			  << std::setw(16) << "DF"
			  << std::setw(16) << "PV" << '\n';

	for (const auto& cashFlow : cashFlows) {
		std::cout << std::setw(10) << std::setprecision(4) << std::fixed << cashFlow.startTime
				  << std::setw(10) << cashFlow.endTime
				  << std::setw(12) << cashFlow.accrualFactor
				  << std::setw(14) << cashFlow.rate
				  << std::setw(16) << cashFlow.amount
				  << std::setw(16) << cashFlow.discountFactor
				  << std::setw(16) << cashFlow.presentValue << '\n';
	}
}

void printMarketSummary(const MarketScenario& scenario) {
	std::cout << "Market inputs\n";
	std::cout << "  Source:   " << scenario.sourceLabel << '\n';
	std::cout << "  As of:    " << scenario.sourceDate << '\n';
	std::cout << "  Deposits: " << scenario.deposits.size() << '\n';
	std::cout << "  Swaps:    " << scenario.swaps.size() << '\n';
	std::cout << "  Trade notional:     " << scenario.notional << '\n';
	std::cout << "  Trade fixed coupon: " << scenario.fixedCoupon << '\n';
	std::cout << "  Trade maturity:     " << scenario.maturity << '\n';
	std::cout << "  Trade interval:     " << scenario.paymentInterval << '\n';
}

} // namespace

int main(int argc, char** argv) {
	try {
		MarketScenario scenario;
		if (argc >= 2) {
			scenario = loadMarketQuotesCsv(argv[1]);
			if (argc >= 3) {
				applyTradeCsv(argv[2], scenario);
			}
		} else {
			// Default to the same public India curve snapshot used by the CSV.
			scenario.sourceLabel = "World Government Bonds India yield curve";
			scenario.sourceDate = "4 Jul 2026";
			scenario.deposits = {{1.0, 0.05715, 1.0}};
			scenario.swaps = {
				{2.0, 0.05995, 1.0},
				{3.0, 0.06212, 1.0},
				{5.0, 0.06425, 1.0},
				{10.0, 0.06707, 1.0}
			};
		}

		printMarketSummary(scenario);

		BootstrappedYieldCurve curve(scenario.deposits, scenario.swaps);

		std::cout << std::fixed << std::setprecision(6);
		std::cout << "Bootstrapped pillars\n";
		auto pillarTimes = curve.pillarTimes();
		auto pillarRates = curve.pillarZeroRates();
		for (std::size_t i = 0; i < pillarTimes.size(); ++i) {
			std::cout << "  t = " << std::setw(4) << pillarTimes[i]
					  << "  zero = " << pillarRates[i]
					  << "  df = " << curve.discountFactor(pillarTimes[i]) << '\n';
		}

		printCurveGrid(curve);

		const double notional = scenario.notional;
		const double fixedCoupon = scenario.fixedCoupon;
		const double maturity = scenario.maturity;
		const double paymentInterval = scenario.paymentInterval;

		InterestRateSwap swap(notional,
							 fixedCoupon,
							 maturity,
							 paymentInterval,
							 scenario.side,
							 scenario.spread,
							 scenario.startTime);
		swap.build(curve);

		printCashFlowTable("\nFixed leg cash flows", swap.fixedLeg().cashFlows());
		printCashFlowTable("\nFloating leg cash flows", swap.floatingLeg().cashFlows());

		const double npv = swap.npv(curve);
		const double fairRate = swap.fairSwapRate(curve);
		const double dv01 = swap.dv01(curve);

		std::cout << "\nSwap valuation summary\n";
		std::cout << "  Notional:      " << notional << '\n';
		std::cout << "  Fixed coupon:  " << fixedCoupon << '\n';
		std::cout << "  Fair par rate: " << fairRate << '\n';
		std::cout << "  NPV:           " << npv << '\n';
		std::cout << "  DV01:          " << dv01 << '\n';

		return 0;
	} catch (const std::exception& ex) {
		std::cerr << "Error: " << ex.what() << '\n';
		std::cerr << "Usage:\n"
		          << "  ./bin/pricer                          # built-in demo\n"
		          << "  ./bin/pricer data/market_quotes.csv    # market CSV\n"
		          << "  ./bin/pricer data/market_quotes.csv data/trade.csv\n";
		return 1;
	}
}
