# Project 2 — Real-Time Traffic Light Control System
**Course:** ENCS4330 — Real-Time Applications & Embedded Systems
**University:** Birzeit University — Faculty of Engineering and Technology
**Instructors:** Dr. Ahmad Afaneh, Dr. Hanna Bullata

---

## Overview

A real-time simulation of a 4-way traffic intersection using multiple Linux
processes communicating through System V IPC. The system manages 4 traffic
lights (North, South, East, West), handles pedestrian crossing requests,
responds to emergency vehicle alerts, and displays everything in a live
OpenGL window.

---

## How It Works

The intersection runs a continuous phase cycle:

```
NS-GREEN (10s) → NS-YELLOW (3s) → ALL-RED (1s) → [pedestrian check]
EW-GREEN (10s) → EW-YELLOW (3s) → ALL-RED (1s) → [pedestrian check]
↻ repeats forever
```

### Interrupts

| Event | What happens |
|-------|-------------|
| **Emergency vehicle** | All conflicting lights go YELLOW → RED, emergency direction goes GREEN, resumes after vehicle passes |
| **Pedestrian request** | Served at the next ALL-RED gap, all lights RED during crossing |
| **Ctrl+C / timeout** | All lights safely transition to RED before shutdown |

### Safety Rules (always enforced)
- Conflicting directions are NEVER simultaneously green
- GREEN → YELLOW → RED transition is always respected (never skips yellow)
- All-red safety gap between every phase change
- Pedestrians only cross when all traffic lights are red

---

## Project Structure

```
traffic/
├── main.c            Creates IPC, forks all processes, runs control logic
├── traffic.h         Shared constants, message struct, IPC helpers
├── traffic_names.c   String arrays for direction/phase/light names
├── shared_state.h    SharedState struct definition
├── shared_state.c    Shared memory create/attach/detach/destroy
├── config.h          Config struct definition
├── config.c          Config file reader
├── control.h         Control function declaration
├── control.c         Phase cycle, emergency handling, pedestrian logic
├── light.h           Light function declaration
├── light.c           Traffic light process (one per direction)
├── vehicle.h         Vehicle detector declaration
├── vehicle.c         Vehicle sensor simulator process
├── pedestrian.h      Pedestrian declaration
├── pedestrian.c      Pedestrian request simulator and watchdog
├── emergency.h       Emergency declaration
├── emergency.c       Emergency vehicle simulator process
├── logger.h          Logger declaration
├── logger.c          Timestamped event logger process
├── display.h         Display declaration
├── display.c         OpenGL animated intersection display
├── config.txt        Configuration file (edit this to change behaviour)
└── Makefile          Build instructions
```

---

## Processes (10 total)

| Process | Role |
|---------|------|
| **main / control** | Creates IPC, forks children, runs the phase cycle logic |
| **Logger** | Receives log messages and writes to traffic.log with timestamps |
| **Light North** | Controls the North traffic light, reports state changes |
| **Light South** | Controls the South traffic light, reports state changes |
| **Light East** | Controls the East traffic light, reports state changes |
| **Light West** | Controls the West traffic light, reports state changes |
| **Vehicle detector** | Every 4 seconds: picks random direction, assigns random car count (0-5) |
| **Pedestrian** | Every 20 seconds: sends a crossing request for a random direction |
| **Emergency** | Every 60 seconds: sends emergency vehicle alert for random direction |
| **Display** | Reads shared memory 60 times/second and draws the OpenGL window |

---

## IPC Mechanisms

### 1. Message Queue
One queue with 6 separate mtype lanes — each process only reads its own lane:

| mtype | Who reads it | What it carries |
|-------|-------------|-----------------|
| 1 | Control | Vehicle events, pedestrian requests, emergency alerts, light confirmations |
| 2 | Light North | CMD_SET_RED / YELLOW / GREEN |
| 3 | Light South | CMD_SET_RED / YELLOW / GREEN |
| 4 | Light East | CMD_SET_RED / YELLOW / GREEN |
| 5 | Light West | CMD_SET_RED / YELLOW / GREEN |
| 6 | Logger | Log messages from any process |

### 2. Shared Memory
One `SharedState` struct mapped by every process simultaneously:
```c
light_state[4]        // current RED/YELLOW/GREEN per direction
current_phase         // which phase is active right now
phase_seconds_left    // countdown for the arc timer
ped_request[4]        // is a pedestrian waiting at each direction?
ped_active            // is someone crossing right now?
vehicles_waiting[4]   // how many cars at each direction (shown as cars)
emergency_active      // is emergency mode on?
emergency_direction   // which direction has the emergency
running               // 1=running, 0=shutdown
safety_violations     // count of GREEN→RED direct transitions detected
```

### 3. Semaphore
Binary mutex — protects all writes to shared memory.
Every write follows: `sem_lock → write → sem_unlock`

---

## Vehicle Priority System

| Priority | Who | Behaviour |
|----------|-----|-----------|
| **Highest** | Emergency vehicle | Overrides everything — all others go RED immediately |
| **Middle** | Pedestrian | Served at every ALL-RED gap — vehicles wait |
| **Lowest** | Regular vehicles | Wait for their turn in the fixed phase cycle |

---

## Configuration File (`config.txt`)

```
green_duration    = 10   # how long green light lasts (seconds)
yellow_duration   = 3    # warning time before red (seconds)
allred_duration   = 1    # safety gap between phases (seconds)
ped_duration      = 8    # pedestrian crossing time (seconds)
ped_max_wait      = 60   # max wait before timing violation logged (seconds)
emg_response_time = 3    # emergency response time reference (seconds)
vehicle_interval  = 4    # how often vehicle counts update (seconds)
ped_interval      = 20   # how often pedestrian requests arrive (seconds)
emg_interval      = 60   # how often emergency vehicles arrive (seconds)
run_duration      = 0    # 0=run forever, any number=seconds to run
```

---

## How to Build and Run

### Install dependencies
```bash
sudo apt-get install -y gcc make freeglut3-dev
```

### Build
```bash
make
```

### Run (runs forever until Ctrl+C)
```bash
./traffic config.txt
```

### Clean
```bash
make clean
```

---

## OpenGL Display

The animated window shows:
- 4-way road intersection with dashed centre lines and stop lines
- 4 traffic light poles with animated glowing bulbs (RED / YELLOW / GREEN)
- Animated cars that spawn based on vehicle detector data, stop at red, drive through on green
- Pedestrian crossing zebra stripes that appear and pulse during a crossing
- Emergency banner that fades in and out when an emergency is active
- Arc-style phase timer (top-right corner) showing seconds remaining
- Info panel (bottom-left) showing current phase, vehicle counts, and stats

---

## Log File (`traffic.log`)

Every important event is recorded with a timestamp:
```
[2026-06-04 14:26:04] [North] Light [North]: RED → GREEN
[2026-06-04 14:26:14] [-]     Phase: NS-YELLOW
[2026-06-04 14:26:17] [West]  EMERGENCY: vehicle detected from West
[2026-06-04 14:26:47] [North] TIMING VIOLATION: Pedestrian waited 31s
```
