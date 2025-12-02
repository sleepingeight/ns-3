import subprocess
import matplotlib.pyplot as plt
import os
import time

def run_ns3_simulation():
    print("Running ns-3 simulation...")
    start_time = time.time()
    
    result = subprocess.run(
        ["./ns3", "run", "reno-equilibrium"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )

    elapsed_time = time.time() - start_time

    if result.returncode != 0:
        print("Error running simulation:")
        print(result.stderr)
        exit(1)

    print(result.stdout)
    return elapsed_time


def parse_cwnd_file(filename):
    times = []
    cwnds = []

    if not os.path.exists(filename):
        print(f"Error: File {filename} not found!")
        exit(1)

    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            
            try:
                t, cwnd = line.split()
                times.append(float(t))
                cwnds.append(int(cwnd))
            except ValueError:
                continue

    return times, cwnds


def plot_cwnd(times, cwnds):
    plt.figure(figsize=(12, 6))
    plt.plot(times, cwnds, linewidth=1.2, color='blue')
    plt.title("TCP Fast Congestion Window Evolution", fontsize=14, fontweight='bold')
    plt.xlabel("Time (s)", fontsize=12)
    plt.ylabel("Congestion Window (bytes)", fontsize=12)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    # Save the plot
    output_file = "results/reno-equilibrium/cwnd_plot.png"
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"Plot saved to: {output_file}")
    
    plt.show()


if __name__ == "__main__":
    # Run simulation
    sim_time = run_ns3_simulation()
    
    # Parse output file
    times, cwnds = parse_cwnd_file("results/reno-equilibrium/cwnd_trace.txt")

    print(f"\nParsed {len(times)} cwnd samples.")
    print(f"Simulation time: {sim_time:.2f} seconds")
    print("Plotting...")

    plot_cwnd(times, cwnds)
    
    print(f"\nTotal runtime: {sim_time:.2f} seconds")
