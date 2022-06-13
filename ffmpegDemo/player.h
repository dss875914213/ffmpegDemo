#pragma once
#include "config.h"
#include <iostream>
using namespace std;

int PlayerRunning(const char* pInputFile);
double GetClock(PlayClock* clock);
void SetClockAt(PlayClock* clock, double pts, int serial, double time);
void SetClock(PlayClock* clock, double pts, int serial);

static PlayerStation* PlayerInit(const char* pInputFile);
static int PlayerDeinit(PlayerStation* is);
static void SetClockSpeed(PlayClock* clock, double speed);
static void InitClock(PlayClock* clock, int* queueSerial);
static void SyncPlayClockToSlave(PlayClock* playClock, PlayClock* slave);
static void DoExit(PlayerStation* is);
static void StreamTogglePause(PlayerStation* is);
static void TogglePause(PlayerStation* is);



