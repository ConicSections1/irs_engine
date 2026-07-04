#include "YieldCurve.h"

#include <algorithm>
#include <limits>

namespace irs {

namespace {

int daysInMonth(int year, int month) {
	static const int lengths[] = {31, 28, 31, 30, 31, 30,
								  31, 31, 30, 31, 30, 31};

	if (month == 2) {
		const bool leapYear = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
		return leapYear ? 29 : 28;
	}

	return lengths[month - 1];
}

long long toSerial(const Date& date) {
	int y = date.year;
	int m = date.month;
	int d = date.day;

	if (m <= 2) {
		y -= 1;
		m += 12;
	}

	const long long era = y / 400;
	const long long yoe = y - era * 400;
	const long long doy = (153 * (m - 3) + 2) / 5 + d - 1;
	const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe;
}

} // namespace

Date Date::addMonths(int months) const {
	int newYear = year;
	int newMonth = month + months;
	while (newMonth > 12) {
		newMonth -= 12;
		++newYear;
	}
	while (newMonth <= 0) {
		newMonth += 12;
		--newYear;
	}

	const int newDay = std::min(day, daysInMonth(newYear, newMonth));
	return {newYear, newMonth, newDay};
}

Date Date::addYears(int years) const {
	return addMonths(years * 12);
}

double Date::yearFraction(const Date& start, const Date& end, DayCountConvention convention) {
	if (convention == DayCountConvention::Act360) {
		return static_cast<double>(toSerial(end) - toSerial(start)) / 360.0;
	}

	int d1 = start.day;
	int d2 = end.day;
	int m1 = start.month;
	int m2 = end.month;
	int y1 = start.year;
	int y2 = end.year;

	if (d1 == 31) {
		d1 = 30;
	}
	if (d2 == 31 && d1 == 30) {
		d2 = 30;
	}

	return (360.0 * (y2 - y1) + 30.0 * (m2 - m1) + static_cast<double>(d2 - d1)) / 360.0;
}

std::vector<double> TimeGrid::regular(double start, double end, double step) {
	std::vector<double> grid;
	if (step <= 0.0 || end < start) {
		return grid;
	}

	const double epsilon = 1e-10;
	for (double t = start; t <= end + epsilon; t += step) {
		grid.push_back(t);
	}
	return grid;
}

double YieldCurve::forwardRate(double t1, double t2) const {
	if (t2 <= t1) {
		return 0.0;
	}

	const double df1 = discountFactor(t1);
	const double df2 = discountFactor(t2);
	const double alpha = t2 - t1;
	return (df1 / df2 - 1.0) / alpha;
}

BootstrappedYieldCurve::BootstrappedYieldCurve(const std::vector<DepositQuote>& deposits,
											   const std::vector<SwapQuote>& swaps) {
	bootstrap(deposits, swaps);
}

double BootstrappedYieldCurve::linearInterpolate(double x0, double y0, double x1, double y1, double x) {
	if (std::abs(x1 - x0) < 1e-12) {
		return y0;
	}
	const double weight = (x - x0) / (x1 - x0);
	return y0 + weight * (y1 - y0);
}

double BootstrappedYieldCurve::interpolatedZeroRate(double t, const std::vector<Pillar>& knownPillars) const {
	if (knownPillars.empty()) {
		return 0.0;
	}

	if (t <= knownPillars.front().time) {
		return knownPillars.front().zeroRate;
	}

	for (std::size_t i = 1; i < knownPillars.size(); ++i) {
		if (t <= knownPillars[i].time) {
			return linearInterpolate(knownPillars[i - 1].time,
									 knownPillars[i - 1].zeroRate,
									 knownPillars[i].time,
									 knownPillars[i].zeroRate,
									 t);
		}
	}

	return knownPillars.back().zeroRate;
}

double BootstrappedYieldCurve::interpolatedZeroRateWithCandidate(double t,
																 double terminalTime,
																 double terminalZeroRate,
																 const std::vector<Pillar>& knownPillars) const {
	if (knownPillars.empty()) {
		return terminalZeroRate;
	}

	if (t <= knownPillars.back().time) {
		return interpolatedZeroRate(t, knownPillars);
	}

	if (t >= terminalTime) {
		return terminalZeroRate;
	}

	return linearInterpolate(knownPillars.back().time,
							 knownPillars.back().zeroRate,
							 terminalTime,
							 terminalZeroRate,
							 t);
}

double BootstrappedYieldCurve::discountFactorWithCandidate(double t,
														   double terminalTime,
														   double terminalZeroRate,
														   const std::vector<Pillar>& knownPillars) const {
	if (t <= 0.0) {
		return 1.0;
	}

	const double zero = interpolatedZeroRateWithCandidate(t, terminalTime, terminalZeroRate, knownPillars);
	return std::exp(-zero * t);
}

double BootstrappedYieldCurve::parSwapResidual(const SwapQuote& swap,
											   double terminalZeroRate,
											   const std::vector<Pillar>& knownPillars) const {
	// The bootstrapping equation is the par-swap condition: the floating leg
	// PV must equal the fixed-leg PV at the market quoted swap rate.
	const double maturity = swap.maturity;
	const double interval = swap.paymentInterval;
	const int numPayments = static_cast<int>(std::lround(maturity / interval));

	double fixedLegPv = 0.0;
	for (int i = 1; i <= numPayments; ++i) {
		const double paymentTime = interval * static_cast<double>(i);
		const double df = discountFactorWithCandidate(paymentTime, maturity, terminalZeroRate, knownPillars);
		fixedLegPv += interval * df;
	}

	const double floatLegPv = 1.0 - std::exp(-terminalZeroRate * maturity);
	return floatLegPv - swap.rate * fixedLegPv;
}

double BootstrappedYieldCurve::solveSwapZeroRate(const SwapQuote& swap,
												 const std::vector<Pillar>& knownPillars) const {
	double low = -0.05;
	double high = 0.20;
	double fLow = parSwapResidual(swap, low, knownPillars);
	double fHigh = parSwapResidual(swap, high, knownPillars);

	int attempts = 0;
	while (fLow * fHigh > 0.0 && attempts < 20) {
		low -= 0.05;
		high += 0.05;
		fLow = parSwapResidual(swap, low, knownPillars);
		fHigh = parSwapResidual(swap, high, knownPillars);
		++attempts;
	}

	if (fLow * fHigh > 0.0) {
		return high;
	}

	for (int i = 0; i < 100; ++i) {
		const double mid = 0.5 * (low + high);
		const double fMid = parSwapResidual(swap, mid, knownPillars);

		if (std::abs(fMid) < 1e-12) {
			return mid;
		}

		if (fLow * fMid <= 0.0) {
			high = mid;
			fHigh = fMid;
		} else {
			low = mid;
			fLow = fMid;
		}
	}

	return 0.5 * (low + high);
}

void BootstrappedYieldCurve::bootstrap(const std::vector<DepositQuote>& deposits,
									   const std::vector<SwapQuote>& swaps) {
	pillars_.clear();

	std::vector<DepositQuote> sortedDeposits = deposits;
	std::vector<SwapQuote> sortedSwaps = swaps;

	std::sort(sortedDeposits.begin(), sortedDeposits.end(), [](const auto& lhs, const auto& rhs) {
		return lhs.maturity < rhs.maturity;
	});

	std::sort(sortedSwaps.begin(), sortedSwaps.end(), [](const auto& lhs, const auto& rhs) {
		return lhs.maturity < rhs.maturity;
	});

	for (const auto& deposit : sortedDeposits) {
		const double df = 1.0 / (1.0 + deposit.rate * deposit.yearFraction);
		const double zero = -std::log(df) / deposit.maturity;
		pillars_.push_back({deposit.maturity, zero});
	}

	for (const auto& swap : sortedSwaps) {
		const double zero = solveSwapZeroRate(swap, pillars_);
		pillars_.push_back({swap.maturity, zero});
	}

	std::sort(pillars_.begin(), pillars_.end(), [](const Pillar& lhs, const Pillar& rhs) {
		return lhs.time < rhs.time;
	});

	std::vector<Pillar> uniquePillars;
	uniquePillars.reserve(pillars_.size());
	for (const auto& pillar : pillars_) {
		if (!uniquePillars.empty() && std::abs(uniquePillars.back().time - pillar.time) < 1e-12) {
			uniquePillars.back() = pillar;
		} else {
			uniquePillars.push_back(pillar);
		}
	}
	pillars_ = std::move(uniquePillars);
}

double BootstrappedYieldCurve::zeroRate(double t) const {
	return interpolatedZeroRate(t, pillars_);
}

double BootstrappedYieldCurve::discountFactor(double t) const {
	if (t <= 0.0) {
		return 1.0;
	}
	return std::exp(-zeroRate(t) * t);
}

std::unique_ptr<YieldCurve> BootstrappedYieldCurve::shifted(double parallelShift) const {
	std::vector<DepositQuote> emptyDeposits;
	std::vector<SwapQuote> emptySwaps;
	auto curve = std::make_unique<BootstrappedYieldCurve>(emptyDeposits, emptySwaps);
	curve->pillars_ = pillars_;
	for (auto& pillar : curve->pillars_) {
		pillar.zeroRate += parallelShift;
	}
	return curve;
}

std::vector<double> BootstrappedYieldCurve::pillarTimes() const {
	std::vector<double> times;
	times.reserve(pillars_.size());
	for (const auto& pillar : pillars_) {
		times.push_back(pillar.time);
	}
	return times;
}

std::vector<double> BootstrappedYieldCurve::pillarZeroRates() const {
	std::vector<double> rates;
	rates.reserve(pillars_.size());
	for (const auto& pillar : pillars_) {
		rates.push_back(pillar.zeroRate);
	}
	return rates;
}

Leg::Leg(double notional, double startTime, double maturity, double paymentInterval)
	: notional_(notional), startTime_(startTime), maturity_(maturity), paymentInterval_(paymentInterval) {}

const std::vector<CashFlow>& Leg::cashFlows() const {
	return cashFlows_;
}

double Leg::presentValue() const {
	double pv = 0.0;
	for (const auto& cashFlow : cashFlows_) {
		pv += cashFlow.presentValue;
	}
	return pv;
}

FixedLeg::FixedLeg(double notional, double fixedRate, double startTime, double maturity, double paymentInterval)
	: Leg(notional, startTime, maturity, paymentInterval), fixedRate_(fixedRate) {}

void FixedLeg::generate(const YieldCurve& curve) {
	cashFlows_.clear();

	const int numPayments = static_cast<int>(std::lround((maturity_ - startTime_) / paymentInterval_));
	double previousTime = startTime_;

	for (int i = 1; i <= numPayments; ++i) {
		const double paymentTime = startTime_ + static_cast<double>(i) * paymentInterval_;
		const double accrual = paymentTime - previousTime;
		const double amount = notional_ * fixedRate_ * accrual;
		const double df = curve.discountFactor(paymentTime);
		const double pv = amount * df;

		cashFlows_.push_back({previousTime, paymentTime, paymentTime, accrual, fixedRate_, amount, df, pv});
		previousTime = paymentTime;
	}
}

double FixedLeg::fixedRate() const {
	return fixedRate_;
}

FloatingLeg::FloatingLeg(double notional, double spread, double startTime, double maturity, double paymentInterval)
	: Leg(notional, startTime, maturity, paymentInterval), spread_(spread) {}

void FloatingLeg::generate(const YieldCurve& curve) {
	cashFlows_.clear();

	const int numPayments = static_cast<int>(std::lround((maturity_ - startTime_) / paymentInterval_));
	double previousTime = startTime_;

	for (int i = 1; i <= numPayments; ++i) {
		const double paymentTime = startTime_ + static_cast<double>(i) * paymentInterval_;
		const double accrual = paymentTime - previousTime;
		// A floating coupon is projected from the discount curve itself:
		// the forward rate implied by DF(t1)/DF(t2) determines the next reset.
		const double forward = curve.forwardRate(previousTime, paymentTime);
		const double effectiveRate = forward + spread_;
		const double amount = notional_ * effectiveRate * accrual;
		const double df = curve.discountFactor(paymentTime);
		const double pv = amount * df;

		cashFlows_.push_back({previousTime, paymentTime, paymentTime, accrual, effectiveRate, amount, df, pv});
		previousTime = paymentTime;
	}
}

double FloatingLeg::spread() const {
	return spread_;
}

InterestRateSwap::InterestRateSwap(double notional,
								   double fixedRate,
								   double maturity,
								   double paymentInterval,
								   Side side,
								   double spread,
								   double startTime)
	: notional_(notional),
	  fixedRate_(fixedRate),
	  maturity_(maturity),
	  paymentInterval_(paymentInterval),
	  side_(side),
	  spread_(spread),
	  startTime_(startTime),
	  fixedLeg_(notional, fixedRate, startTime, maturity, paymentInterval),
	  floatingLeg_(notional, spread, startTime, maturity, paymentInterval) {}

void InterestRateSwap::build(const YieldCurve& curve) {
	fixedLeg_.generate(curve);
	floatingLeg_.generate(curve);
}

double InterestRateSwap::npv(const YieldCurve& curve) const {
	FixedLeg fixedLeg(notional_, fixedRate_, startTime_, maturity_, paymentInterval_);
	FloatingLeg floatingLeg(notional_, spread_, startTime_, maturity_, paymentInterval_);
	fixedLeg.generate(curve);
	floatingLeg.generate(curve);

	const double fixedPv = fixedLeg.presentValue();
	const double floatingPv = floatingLeg.presentValue();

	if (side_ == Side::PayFixed) {
		return floatingPv - fixedPv;
	}
	return fixedPv - floatingPv;
}

double InterestRateSwap::fairSwapRate(const YieldCurve& curve) const {
	// The par swap rate is the fixed coupon that makes the swap value zero,
	// i.e. floating PV divided by the fixed-leg annuity.
	FixedLeg unitFixedLeg(notional_, 1.0, startTime_, maturity_, paymentInterval_);
	FloatingLeg floatingLeg(notional_, spread_, startTime_, maturity_, paymentInterval_);
	unitFixedLeg.generate(curve);
	floatingLeg.generate(curve);

	const double annuity = unitFixedLeg.presentValue();
	if (std::abs(annuity) < 1e-12) {
		return 0.0;
	}
	return floatingLeg.presentValue() / annuity;
}

double InterestRateSwap::dv01(const YieldCurve& curve) const {
	// DV01 is estimated by a parallel 1bp shift on the full zero curve.
	const auto up = curve.shifted(0.0001);
	const auto down = curve.shifted(-0.0001);
	const double npvUp = npv(*up);
	const double npvDown = npv(*down);
	return std::abs(npvUp - npvDown) / 2.0;
}

const FixedLeg& InterestRateSwap::fixedLeg() const {
	return fixedLeg_;
}

const FloatingLeg& InterestRateSwap::floatingLeg() const {
	return floatingLeg_;
}

} // namespace irs
