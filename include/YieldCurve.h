#pragma once

#include <cmath>
#include <memory>
#include <vector>

namespace irs {

enum class DayCountConvention {
	Act360,
	Thirty360
};

struct Date {
	int year{0};
	int month{0};
	int day{0};

	Date() = default;
	Date(int y, int m, int d) : year(y), month(m), day(d) {}

	Date addMonths(int months) const;
	Date addYears(int years) const;
	static double yearFraction(const Date& start, const Date& end, DayCountConvention convention);
};

struct TimeGrid {
	static std::vector<double> regular(double start, double end, double step);
};

struct DepositQuote {
	double maturity{0.0};
	double rate{0.0};
	double yearFraction{1.0};
};

struct SwapQuote {
	double maturity{0.0};
	double rate{0.0};
	double paymentInterval{1.0};
};

class YieldCurve {
public:
	virtual ~YieldCurve() = default;

	// The curve is queried through continuously compounded zero rates,
	// from which discount factors and forward rates are derived.
	virtual double zeroRate(double t) const = 0;
	virtual double discountFactor(double t) const = 0;
	virtual std::unique_ptr<YieldCurve> shifted(double parallelShift) const = 0;

	virtual double forwardRate(double t1, double t2) const;
};

class BootstrappedYieldCurve final : public YieldCurve {
public:
	BootstrappedYieldCurve(const std::vector<DepositQuote>& deposits,
						   const std::vector<SwapQuote>& swaps);

	double zeroRate(double t) const override;
	double discountFactor(double t) const override;
	std::unique_ptr<YieldCurve> shifted(double parallelShift) const override;

	std::vector<double> pillarTimes() const;
	std::vector<double> pillarZeroRates() const;

private:
	struct Pillar {
		double time{0.0};
		double zeroRate{0.0};
	};

	std::vector<Pillar> pillars_;

	void bootstrap(const std::vector<DepositQuote>& deposits,
				   const std::vector<SwapQuote>& swaps);

	static double linearInterpolate(double x0, double y0, double x1, double y1, double x);
	double interpolatedZeroRate(double t, const std::vector<Pillar>& knownPillars) const;
	double interpolatedZeroRateWithCandidate(double t, double terminalTime, double terminalZeroRate,
											 const std::vector<Pillar>& knownPillars) const;
	double discountFactorWithCandidate(double t, double terminalTime, double terminalZeroRate,
									   const std::vector<Pillar>& knownPillars) const;
	double parSwapResidual(const SwapQuote& swap, double terminalZeroRate,
						   const std::vector<Pillar>& knownPillars) const;
	double solveSwapZeroRate(const SwapQuote& swap, const std::vector<Pillar>& knownPillars) const;
};

struct CashFlow {
	double startTime{0.0};
	double endTime{0.0};
	double paymentTime{0.0};
	double accrualFactor{0.0};
	double rate{0.0};
	double amount{0.0};
	double discountFactor{0.0};
	double presentValue{0.0};
};

class Leg {
public:
	explicit Leg(double notional, double startTime, double maturity, double paymentInterval);
	virtual ~Leg() = default;

	virtual void generate(const YieldCurve& curve) = 0;

	const std::vector<CashFlow>& cashFlows() const;
	double presentValue() const;

protected:
	double notional_{0.0};
	double startTime_{0.0};
	double maturity_{0.0};
	double paymentInterval_{1.0};
	std::vector<CashFlow> cashFlows_;
};

class FixedLeg final : public Leg {
public:
	FixedLeg(double notional, double fixedRate, double startTime, double maturity, double paymentInterval);

	void generate(const YieldCurve& curve) override;

	double fixedRate() const;

private:
	double fixedRate_{0.0};
};

class FloatingLeg final : public Leg {
public:
	FloatingLeg(double notional, double spread, double startTime, double maturity, double paymentInterval);

	void generate(const YieldCurve& curve) override;

	double spread() const;

private:
	double spread_{0.0};
};

class InterestRateSwap {
public:
	enum class Side {
		PayFixed,
		ReceiveFixed
	};

	InterestRateSwap(double notional,
					 double fixedRate,
					 double maturity,
					 double paymentInterval,
					 Side side = Side::PayFixed,
					 double spread = 0.0,
					 double startTime = 0.0);

	void build(const YieldCurve& curve);

	double npv(const YieldCurve& curve) const;
	double fairSwapRate(const YieldCurve& curve) const;
	double dv01(const YieldCurve& curve) const;

	const FixedLeg& fixedLeg() const;
	const FloatingLeg& floatingLeg() const;

private:
	double notional_{0.0};
	double fixedRate_{0.0};
	double maturity_{0.0};
	double paymentInterval_{1.0};
	Side side_{Side::PayFixed};
	double spread_{0.0};
	double startTime_{0.0};

	FixedLeg fixedLeg_;
	FloatingLeg floatingLeg_;
};

} // namespace irs
