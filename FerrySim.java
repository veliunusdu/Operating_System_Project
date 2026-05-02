import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import java.util.concurrent.locks.*;

/**
 * Ferry Transport Simulation
 * Implementation of a multithreaded system with vehicles and a ferry.
 */
public class FerrySim {

    // --- Configuration ---
    private static final int NUM_CARS = 12;
    private static final int NUM_MINIBUSES = 10;
    private static final int NUM_TRUCKS = 8;
    private static final int TOTAL_VEHICLES = NUM_CARS + NUM_MINIBUSES + NUM_TRUCKS;
    private static final int FERRY_MAX_LOAD = 20;
    private static final int TOLL_PER_SIDE = 2;
    private static final int DEPARTURE_TIMEOUT_MS = 4000;

    // --- Shared State ---
    private static final long simStartMs = System.currentTimeMillis();
    private static final AtomicInteger completedCount = new AtomicInteger(0);
    
    // Statistics
    private static long maxWaitMs = 0;
    private static long sumWaitMs = 0;
    private static int totalTrips = 0;
    private static final Object statsLock = new Object();

    // Side data
    private static final Side[] sides = new Side[2];

    // Ferry data
    private static final Ferry ferry = new Ferry();

    // Enum for Sides
    enum SideID {
        SIDE_A(0, "Side-A"), SIDE_B(1, "Side-B");
        final int id;
        final String name;
        SideID(int id, String name) { this.id = id; this.name = name; }
        SideID opposite() { return this == SIDE_A ? SIDE_B : SIDE_A; }
    }

    // Vehicle types
    enum VehicleType {
        CAR("Car", 1), MINIBUS("Minibus", 2), TRUCK("Truck", 3);
        final String name;
        final int capacity;
        VehicleType(String name, int capacity) { this.name = name; this.capacity = capacity; }
    }

    // --- Utility ---
    private static void log(String fmt, Object... args) {
        long elapsed = System.currentTimeMillis() - simStartMs;
        synchronized (System.out) {
            System.out.printf("[%6d ms] %s%n", elapsed, String.format(fmt, args));
        }
    }

    private static void sleep(int ms) {
        try { Thread.sleep(ms); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
    }

    private static int randRange(int lo, int hi) {
        return lo + new Random().nextInt(hi - lo + 1);
    }

    // --- Side Class ---
    static class Side {
        final SideID id;
        final Semaphore tollBooths = new Semaphore(TOLL_PER_SIDE, true);
        final ArrayBlockingQueue<Vehicle> queue = new ArrayBlockingQueue<>(TOTAL_VEHICLES);
        final ReentrantLock queueLock = new ReentrantLock(true);
        final Condition vehicleReady = queueLock.newCondition();

        Side(SideID id) { this.id = id; }
    }

    // --- Vehicle Class ---
    static class Vehicle implements Runnable {
        final int id;
        final VehicleType type;
        final SideID originSide;
        SideID currentSide;
        long totalWaitMs = 0;

        Vehicle(int id, VehicleType type, SideID origin) {
            this.id = id;
            this.type = type;
            this.originSide = origin;
            this.currentSide = origin;
        }

        @Override
        public void run() {
            log("%s-%d CREATED — origin %s, capacity=%d", type.name, id, originSide.name, type.capacity);
            sleep(randRange(50, 600)); // Stagger start

            // Trip 1: Origin -> Destination
            performTrip(originSide, originSide.opposite());

            // Wait before return
            int waitTime = randRange(800, 2500);
            log("%s-%d waiting %d ms before return trip", type.name, id, waitTime);
            sleep(waitTime);

            // Trip 2: Destination -> Origin
            performTrip(originSide.opposite(), originSide);

            log("%s-%d COMPLETED round trip (total wait = %d ms | completed %d/%d)",
                type.name, id, totalWaitMs, completedCount.incrementAndGet(), TOTAL_VEHICLES);
        }

        private void performTrip(SideID from, SideID to) {
            // 1. Toll
            log("%s-%d waiting for toll at %s", type.name, id, from.name);
            try {
                sides[from.id].tollBooths.acquire();
                log("%s-%d entered toll booth at %s", type.name, id, from.name);
                sleep(randRange(100, 500));
                log("%s-%d exited toll booth at %s", type.name, id, from.name);
                sides[from.id].tollBooths.release();
            } catch (InterruptedException e) { Thread.currentThread().interrupt(); return; }

            // 2. Queue & Board
            long queueStart = System.currentTimeMillis();
            Side s = sides[from.id];
            s.queue.add(this);
            log("%s-%d joined queue at %s (queue size: %d)", type.name, id, from.name, s.queue.size());

            synchronized (this) {
                // Signal ferry that someone joined
                s.queueLock.lock();
                try { s.vehicleReady.signal(); } finally { s.queueLock.unlock(); }

                // Wait for ferry to pick us
                while (this.currentSide == from) {
                    try { this.wait(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
                }
            }
            long waitTime = System.currentTimeMillis() - queueStart;
            this.totalWaitMs += waitTime;
            synchronized (statsLock) {
                sumWaitMs += waitTime;
                if (waitTime > maxWaitMs) maxWaitMs = waitTime;
            }

            // 3. Unload
            synchronized (this) {
                while (this.currentSide == null) { // null means on ferry
                    try { this.wait(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
                }
            }
            log("%s-%d UNLOADED at %s", type.name, id, to.name);
        }
    }

    // --- Ferry Class ---
    static class Ferry implements Runnable {
        SideID currentSide = SideID.values()[new Random().nextInt(2)];
        int currentLoad = 0;
        final List<Vehicle> passengers = new ArrayList<>();
        boolean isRunning = true;

        @Override
        public void run() {
            log("Ferry CREATED — starting on %s", currentSide.name);

            while (completedCount.get() < TOTAL_VEHICLES) {
                // 1. Unload
                if (!passengers.isEmpty()) {
                    log("Ferry ARRIVED at %s (load=%d units) — unloading", currentSide.name, currentLoad);
                    for (Vehicle v : passengers) {
                        synchronized (v) {
                            v.currentSide = currentSide;
                            v.notify();
                        }
                        sleep(50); // Small delay for realism
                    }
                    passengers.clear();
                    currentLoad = 0;
                    log("Ferry unloading COMPLETE at %s", currentSide.name);
                } else {
                    log("Ferry ARRIVED at %s (empty) — ready to load", currentSide.name);
                }

                // 2. Load
                Side s = sides[currentSide.id];
                long deadline = System.currentTimeMillis() + DEPARTURE_TIMEOUT_MS;

                while (true) {
                    Vehicle next = s.queue.peek();
                    
                    // Departure conditions
                    if (currentLoad == FERRY_MAX_LOAD) {
                        log("Ferry FULL (%d units). Departing %s", currentLoad, currentSide.name);
                        break;
                    }
                    if (next != null && currentLoad + next.type.capacity > FERRY_MAX_LOAD) {
                        log("Head vehicle (%d units) does not fit (remaining=%d). Departing %s",
                            next.type.capacity, FERRY_MAX_LOAD - currentLoad, currentSide.name);
                        break;
                    }
                    if (next == null) {
                        long waitTime = deadline - System.currentTimeMillis();
                        if (waitTime <= 0) {
                            log("Ferry timeout — departing %s with load=%d units", currentSide.name, currentLoad);
                            break;
                        }
                        // Wait for a vehicle to join
                        s.queueLock.lock();
                        try {
                            s.vehicleReady.await(waitTime, TimeUnit.MILLISECONDS);
                        } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
                        finally { s.queueLock.unlock(); }
                        continue; // Re-check
                    }

                    // Board vehicle
                    next = s.queue.poll();
                    log("%s-%d BOARDED at %s (ferry load = %d/%d units)",
                        next.type.name, next.id, currentSide.name, currentLoad + next.type.capacity, FERRY_MAX_LOAD);
                    
                    synchronized (next) {
                        next.currentSide = null; // on ferry
                        next.notify();
                    }
                    passengers.add(next);
                    currentLoad += next.type.capacity;
                    deadline = System.currentTimeMillis() + DEPARTURE_TIMEOUT_MS; // Reset timeout
                }

                // 3. Travel
                SideID nextSide = currentSide.opposite();
                log("Ferry DEPARTED %s -> %s [trip #%d | load=%d units | %d vehicles]",
                    currentSide.name, nextSide.name, ++totalTrips, currentLoad, passengers.size());
                
                sleep(randRange(1500, 3000));
                currentSide = nextSide;

                if (completedCount.get() == TOTAL_VEHICLES) break;
            }
            log("Ferry thread FINISHED. Total trips: %d", totalTrips);
        }
    }

    // --- Main ---
    public static void main(String[] args) {
        System.out.println("╔══════════════════════════════════════════════════╗");
        System.out.println("║       FERRY TRANSPORT SIMULATION — START         ║");
        System.out.println("╚══════════════════════════════════════════════════╝\n");

        sides[0] = new Side(SideID.SIDE_A);
        sides[1] = new Side(SideID.SIDE_B);

        List<Vehicle> allVehicles = new ArrayList<>();
        int id = 0;
        for (int i = 0; i < NUM_CARS; i++) allVehicles.add(new Vehicle(id++, VehicleType.CAR, randomSide()));
        for (int i = 0; i < NUM_MINIBUSES; i++) allVehicles.add(new Vehicle(id++, VehicleType.MINIBUS, randomSide()));
        for (int i = 0; i < NUM_TRUCKS; i++) allVehicles.add(new Vehicle(id++, VehicleType.TRUCK, randomSide()));

        Thread ferryThread = new Thread(ferry);
        ferryThread.start();

        List<Thread> vehicleThreads = new ArrayList<>();
        for (Vehicle v : allVehicles) {
            Thread t = new Thread(v);
            t.start();
            vehicleThreads.add(t);
        }

        for (Thread t : vehicleThreads) {
            try { t.join(); } catch (InterruptedException e) { e.printStackTrace(); }
        }

        try { ferryThread.join(); } catch (InterruptedException e) { e.printStackTrace(); }

        printStats(allVehicles);
    }

    private static SideID randomSide() {
        return SideID.values()[new Random().nextInt(2)];
    }

    private static void printStats(List<Vehicle> allVehicles) {
        long totalTime = System.currentTimeMillis() - simStartMs;
        System.out.println("\n╔══════════════════════════════════════════════════╗");
        System.out.println("║           SIMULATION STATISTICS REPORT           ║");
        System.out.println("╠══════════════════════════════════════════════════╣");
        System.out.printf("║  Total simulation time   : %8d ms            ║%n", totalTime);
        System.out.printf("║  Total ferry trips       : %8d               ║%n", totalTrips);
        System.out.printf("║  Vehicles completed      : %8d / %-3d          ║%n", completedCount.get(), TOTAL_VEHICLES);
        System.out.printf("║  Average wait per vehicle: %8d ms            ║%n", sumWaitMs / TOTAL_VEHICLES);
        System.out.printf("║  Maximum wait (any veh.) : %8d ms            ║%n", maxWaitMs);
        
        long totalUnits = (long) (NUM_CARS + 2 * NUM_MINIBUSES + 3 * NUM_TRUCKS) * 2;
        double util = totalTrips > 0 ? (double) totalUnits / (totalTrips * FERRY_MAX_LOAD) * 100.0 : 0.0;
        System.out.printf("║  Ferry utilisation ratio : %7.1f %%             ║%n", util);
        System.out.println("╚══════════════════════════════════════════════════╝\n");

        System.out.printf("  %-10s  %5s  %8s  %12s%n", "Vehicle", "Cap", "Origin", "Wait (ms)");
        System.out.printf("  %-10s  %5s  %8s  %12s%n", "----------", "---", "--------", "------------");
        for (Vehicle v : allVehicles) {
            System.out.printf("  %-10s  %5d  %8s  %12d%n",
                v.type.name + "-" + v.id, v.type.capacity, v.originSide.name, v.totalWaitMs);
        }
    }
}
