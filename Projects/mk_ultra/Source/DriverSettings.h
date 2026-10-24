#pragma once

// Every Driver tuning number, loaded from Content/Settings/Driver.settings. Nothing here has a
// gameplay default on purpose: the file is the one place numbers live (design doc, Section 6).
#define DRIVER_SETTINGS(X) \
	X(MatchLength) X(CrossingTime) X(WeatherWindowHours) X(TimeBonusPerSecond) \
	X(OxygenPerPersonPerDay) X(CrewSize) X(ColonyCrew) X(ColonyReserveKg) X(StartingCargo) \
	X(CruiseSpeed) X(ThrustAccel) X(BrakeDecel) X(MouseSensitivity) X(MouseJumpLimit) X(MaxPitch) X(TurnTime90) \
	X(CalmSolarWind) X(StormSolarWind) X(CalmWobble) X(StormWobble) X(WobbleFrequency) \
	X(CalmKp) X(StormKp) X(CalmJerkInterval) X(StormJerkInterval) X(JerkStrength) X(JerkDecay) \
	X(FetchLive) X(FetchTimeout) \
	X(SteadyTime) X(DriftRampTime) X(WalkSpeed) X(LookSensitivity) X(EyeHeight) X(InteractRange) X(CabWallMargin) \
	X(ScrubberRepairTime) X(Co2RiseTime) X(Co2FallTime) X(BlackoutTime) X(Co2AfterBlackout) X(Co2DarkenMax) X(Co2WobbleBoost) \
	X(CabReserveTime) X(PowerFlickerTime) X(PowerFlickerLevel) \
	X(TicketCostFraction) X(CalmHandsTime) X(JackpotTickets) X(PrizeDud) X(PrizeCalmHands) X(PrizeFreeFix) X(PrizeJackpot) \
	X(CalmHandsScale) X(TicketColumns) X(TicketRows) X(TicketRevealFraction) X(TicketShowTime) X(TicketCoinSpeed) X(TicketCoinRadius) \
	X(DeliveryRadius) \
	X(MaxClusters) X(RocksPerCluster) X(ClusterSpread) X(MinRockDiameter) X(MaxRockDiameter) X(MinRockSize) X(MaxRockSize) \
	X(MinFlybySpeed) X(MaxFlybySpeed) X(MinDrift) X(MaxDrift) X(MaxMissLunar) X(MaxRouteOffset) X(RouteClusterStart) X(RouteClusterEnd) \
	X(TruckRadius) X(BounceSpeedKeep) X(BounceJerk) \
	X(ChaseDistance) X(ChaseHeight) X(SeatedEyeHeight) X(BankPerTurn) X(BankMax) X(BankSmoothing) X(DustCount) X(DustSpread) X(HudDistance) X(HudAspect) X(HudTextSize)

struct DriverSettings
{
#define DRIVER_SETTING_FIELD(name) float name = 0;
	DRIVER_SETTINGS(DRIVER_SETTING_FIELD)
#undef DRIVER_SETTING_FIELD
};

// Reads "Name value" lines; # starts a note. Logs unknown or missing names. False if the file can't be read.
bool LoadDriverSettings(const char* path, DriverSettings& out);
