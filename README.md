# Ferry Transport Simulation

This project is a multithreaded simulation of a ferry transport system, implemented in both Java and C. It models vehicles (cars, minibuses, and trucks) traveling between two sides (Side A and Side B) using a single ferry. 

The simulation utilizes threads, mutexes/locks, condition variables, and semaphores to coordinate the loading, unloading, and travel of vehicles.

## Running the Java Implementation

### Prerequisites
- Java Development Kit (JDK) 8 or higher installed.

### Compilation and Execution
1. Open a terminal or command prompt.
2. Navigate to the project directory:
   ```bash
   cd c:\Codes\Projects\OS-Project
   ```
3. Compile the Java file:
   ```bash
   javac FerrySim.java
   ```
4. Run the compiled class:
   ```bash
   java FerrySim
   ```

## Running the C Implementation

### Prerequisites
- GCC compiler installed (typically available on Linux/macOS, or via MinGW/WSL on Windows).
- POSIX threads library (usually included with GCC on POSIX systems).

### Compilation and Execution
1. Open a terminal or command prompt.
2. Navigate to the project directory:
   ```bash
   cd c:\Codes\Projects\OS-Project
   ```
3. Compile the C files:
   ```bash
   gcc -o ferry_sim ferry_sim.c -lpthread -lrt
   ```
4. Run the compiled executable:
   ```bash
   ./ferry_sim
   ```

## Project Files
- `FerrySim.java`: Java implementation.
- `ferry_sim.c` / `ferry_sim.h`: C implementation.
- `REPORT.md`: Detailed system design and synchronization strategy.
- `SAMPLE_OUTPUT.txt`: Example run output.