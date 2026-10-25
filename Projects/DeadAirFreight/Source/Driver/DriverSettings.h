#pragma once

#define DRIVER_SETTINGS(X) \
	X(MatchLength) X(CrossingTime) X(WeatherWindowHours) X(TimeBonusPerSecond) X(ParTime) \
	X(OxygenPerPersonPerDay) X(CrewSize) X(ColonyCrew) X(ColonyReserveKg) X(StartingCargo) \
	X(CruiseSpeed) X(ThrustAccel) X(MouseSensitivity) X(MaxPitch) X(TurnTime90) X(NoseTurnRate) X(NoseTurnAccel) \
	X(CalmSolarWind) X(StormSolarWind) X(CalmWobble) X(StormWobble) X(WobbleFrequency) \
	X(CalmKp) X(StormKp) X(CalmJerkInterval) X(StormJerkInterval) X(JerkStrength) X(JerkDecay) \
	X(FetchLive) X(FetchTimeout) \
	X(SteadyTime) X(DriftRampTime) X(WalkSpeed) X(LookSensitivity) X(EyeHeight) X(InteractRange) X(CabWallMargin) \
	X(ScrubberRepairTime) X(SpareCanisters) X(Co2RiseTime) X(Co2FallTime) X(BlackoutTime) X(Co2AfterBlackout) X(Co2DarkenMax) X(Co2WobbleBoost) \
	X(CabReserveTime) X(PowerFlickerTime) X(PowerFlickerLevel) \
	X(TicketCostFraction) X(CalmHandsTime) X(JackpotTickets) X(BoostTime) X(BoostSpeedScale) X(BoostAccel) X(BoostFov) X(BoostShake) X(BoostJerkInterval) X(BoostJerkStrength) X(PrizeDud) X(PrizeCalmHands) X(PrizeFreeFix) X(PrizeJackpot) \
	X(CalmHandsScale) X(TicketColumns) X(TicketRows) X(TicketRevealFraction) X(TicketShowTime) X(TicketCoinSpeed) X(TicketCoinRadius) \
	X(ColonyRadius) X(DeliveryAltitude) X(KmPerUnit) \
	X(MaxClusters) X(RocksPerCluster) X(ClusterSpread) X(MinRockDiameter) X(MaxRockDiameter) X(MinRockSize) X(MaxRockSize) \
	X(MinFlybySpeed) X(MaxFlybySpeed) X(MinDrift) X(MaxDrift) X(MaxMissLunar) X(MaxRouteOffset) X(RouteClusterStart) X(RouteClusterEnd) \
	X(TruckRadius) X(BounceSpeedKeep) X(BounceJerk) \
	X(BlackHoleCount) X(BlackHoleCoreRadius) X(BlackHoleRingRadius) X(BlackHoleRouteOffset) X(BlackHolePull) X(SlingshotTime) \
	X(ChaseDistance) X(ChaseHeight) X(SeatedEyeHeight) X(BankPerTurn) X(BankMax) X(BankSmoothing) X(DustCount) X(DustSpread) X(StreakLength) X(StreakSpeed)

struct DriverSettings
{
#define DRIVER_SETTING_FIELD(name) float name = 0;
	DRIVER_SETTINGS(DRIVER_SETTING_FIELD)
#undef DRIVER_SETTING_FIELD
};

bool LoadDriverSettings(const char* path, DriverSettings& out);
