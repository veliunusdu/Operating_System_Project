Project Title: Ferry Transport Simulation (Multithreaded System)
________________________________________
## 1. System Design
The system simulates a ferry operating between two sides (Side A and Side B). It is implemented in **Java** and consists of three main components:
- **Vehicle Threads**: Each vehicle (Car, Minibus, Truck) is an independent thread that performs a round trip.
- **Ferry Thread**: A central controller that handles loading, traveling, and unloading.
- **Side Management**: Handles toll booths and waiting areas (FIFO queues).

## 2. Thread Structure
- **Ferry Thread**: Operates in a loop:
    1. **Unloading Phase**: If passengers are on board, they are notified one by one to "unload". The ferry waits for them to acknowledge.
    2. **Loading Phase**: Checks the FIFO queue on the current side. It grants boarding to the head vehicle if capacity allows. It repeats this until the ferry is full, no more vehicles fit, or a timeout is reached.
    3. **Travel Phase**: Simulates the crossing with a random delay, then switches the current side.
- **Vehicle Threads**: Each vehicle follows this lifecycle twice (round trip):
    1. **Toll Entry**: Acquires a semaphore representing one of the 2 toll booths per side.
    2. **Queue Entry**: Joins a FIFO queue and waits for the Ferry's "boarding signal".
    3. **Boarding**: Updates ferry load and waits for arrival at the destination.
    4. **Unloading**: Updates its state and waits for a random delay before starting the return trip.

## 3. Synchronization Strategy
The simulation leverages Java's native concurrency features to ensure a robust, race-free environment:
- **Semaphores (`tollBooths`)**: A counting semaphore manages access to the 4 toll booths (2 per side), ensuring that no more than 2 vehicles per side occupy the booths simultaneously.
- **ReentrantLocks & Conditions (`queueLock`, `vehicleReady`)**: Used for the waiting area. The `ReentrantLock` provides mutual exclusion for the queue, while the `Condition` object allows the Ferry thread to `await()` for vehicles to join the queue without consuming CPU cycles (no busy-waiting).
- **Native Monitor Locks (`synchronized`)**: Used for the boarding/unloading handshake between the Ferry and individual `Vehicle` objects. Using `wait()` and `notify()` on the vehicle object allows for a lightweight, precise signaling mechanism.
- **AtomicInteger (`completedCount`)**: Provides lock-free updates to the total count of completed round trips, which is used to determine when the simulation ends.
- **FIFO Discipline**: Guaranteed by the `ArrayBlockingQueue`, ensuring vehicles are served in the exact order they entered the waiting area.

## 4. Fairness and Deadlock Discussion
### 4.1 Fairness
- **Side Fairness**: The ferry alternates between Side A and Side B after every departure. This ensures that neither side is ignored indefinitely.
- **Two-Side Coordination (7.2)**: To minimize global waiting times, the ferry dynamically checks the opposite side. If the current side is empty but the opposite side has vehicles waiting, the ferry skips the departure timeout and crosses immediately.
- **Vehicle Fairness**: Within each side, vehicles are handled using a **FIFO queue**. The ferry always checks the head of the queue, ensuring that vehicles are served in their arrival order.
- **Starvation Prevention**: A **departure timeout** (4000ms) ensures that the ferry departs even if it's not full, preventing vehicles from waiting forever on a low-traffic side.

### 4.2 Deadlock Avoidance
Deadlock is avoided through a clear hierarchy of locks and state transitions:
- The Ferry thread never holds a lock while waiting for a "slow" event; it uses `await()` or `wait()` which atomically releases the lock.
- Locks are always acquired in a consistent order: first the Side-specific locks, then the Vehicle-specific monitor locks.
- The use of high-level Java primitives reduces the risk of manual locking errors that typically lead to inconsistent states.

## 5. Assumptions
- **Instant Boarding/Unloading**: While there is a small delay for realism, the boarding/unloading process is treated as a critical section.
- **No Mechanical Failure**: The ferry and toll booths are assumed to be 100% reliable.
- **Uniform Travel Time**: The travel time range is the same for both directions.

## 6. Advanced Features (Bonus)
- **Configurable Parameters**: The simulation parameters are no longer hardcoded. The system accepts command-line arguments to customize:
    - Number of Cars, Minibuses, and Trucks.
    - Maximum Ferry Capacity.
    - Departure Timeout (ms).
  Usage: `java FerrySim [cars] [minibuses] [trucks] [max_load] [timeout_ms]`
