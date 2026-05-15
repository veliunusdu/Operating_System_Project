# Ferry Transport Simulation

This project is a multithreaded simulation of a ferry transport system, implemented in **Java**. It models vehicles (cars, minibuses, and trucks) traveling between two sides (Side A and Side B) using a single ferry. 

The simulation utilizes threads, locks, condition variables, and semaphores to coordinate the loading, unloading, and travel of vehicles.

## Prerequisites
- Java Development Kit (JDK) 8 or higher installed.

## Compilation and Execution

You can use the provided `Makefile` to run the simulation:
```bash
make run
```

Alternatively, you can compile and run manually:
1. Compile the Java file:
   ```bash
   javac FerrySim.java
   ```
2. Run the compiled class:
   ```bash
   java FerrySim
   ```

## Project Files
- `FerrySim.java`: Main Java implementation.
- `Makefile`: Build automation file.
- `REPORT.md`: Detailed system design and synchronization strategy.
- `SAMPLE_OUTPUT.txt`: Example run output.
- `README.md`: This project documentation.# Operating_System_Project
