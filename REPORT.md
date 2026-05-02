# Ferry Transport Simulation - Project Report

## 1. System Design
The system simulates a ferry operating between two sides (Side A and Side B). It consists of three main components:
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
The simulation uses several high-level synchronization primitives to ensure safety and order:
- **Semaphores (`tollBooths`)**: Used to limit access to the 2 toll booths per side. Only 2 vehicles can be "at the toll" simultaneously per side.
- **Locks & Conditions (`queueLock`, `vehicleReady`)**: Used in the waiting area. When a vehicle joins the queue, it signals the Ferry (which might be waiting for a vehicle to arrive).
- **Monitor Locks (`synchronized(v)`)**: Used for the boarding/unloading handshake between the Ferry and specific Vehicles. This ensures that a vehicle only proceeds when the ferry has explicitly picked it or allowed it to unload.
- **Atomic Variables (`completedCount`)**: Tracks the total number of finished round trips to determine when the simulation ends.
- **Thread Safety**: All shared data (queues, statistics, ferry state) are protected by either thread-safe collections (`ArrayBlockingQueue`) or explicit synchronization.

## 4. Fairness and Deadlock Discussion
### 4.1 Fairness
- **Side Fairness**: The ferry alternates between Side A and Side B after every departure. This ensures that neither side is ignored indefinitely.
- **Vehicle Fairness**: Within each side, vehicles are handled using a **FIFO queue**. The ferry always checks the head of the queue, ensuring that vehicles are served in their arrival order.
- **Starvation Prevention**: A **departure timeout** (4000ms) ensures that the ferry departs even if it's not full, preventing vehicles from waiting forever on a low-traffic side.

### 4.2 Deadlock Avoidance
Deadlock is avoided through a clear hierarchy of locks and state transitions:
- The Ferry thread never holds a lock while waiting for a "slow" event (it uses `await` or `notify` on specific vehicles).
- Locks are always acquired in a consistent order: first the Side-specific locks, then the Vehicle-specific monitor locks.
- No "busy waiting" is used; all threads use condition-based waiting (`wait()`/`notify()`, `await()`/`signal()`), which releases the associated lock while waiting.

## 5. Assumptions
- **Instant Boarding/Unloading**: While there is a small delay for realism, the boarding/unloading process is treated as a critical section where only one vehicle moves at a time.
- **No Mechanical Failure**: The ferry and toll booths are assumed to be 100% reliable.
- **Uniform Travel Time**: The travel time range is the same for both directions.
