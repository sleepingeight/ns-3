#!/usr/bin/env python3
"""
Run fanout topology simulations and generate comparison plots
Topology: 1 server → 5 routers → 15 receivers (heterogeneous paths)
Comparing TcpLinuxReno vs TcpFast
"""

import subprocess
import csv
import os
import sys
import time

# Check for matplotlib
try:
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("Warning: matplotlib not found. Plots will not be generated.")
    print("Install with: pip3 install matplotlib numpy")

TCP_VARIANTS = ["LinuxReno", "Fast"]
SIMULATION_TIME = 60  # Longer for high-delay network
OUTPUT_DIR = "results/og-sim-2/"

# Get the ns-3 root directory
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
NS3_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..'))

def run_simulation(tcp_variant, sim_time):
    """Run a single simulation"""
    print(f"\n{'='*80}")
    print(f"Running simulation for {tcp_variant}...")
    print(f"{'='*80}\n")
    
    # Change to ns-3 root directory
    os.chdir(NS3_ROOT)
    
    cmd = [
        "./ns3", "run",
        f"og-sim-2 --tcpVariant={tcp_variant} --simulationTime={sim_time}"
    ]
    
    try:
        result = subprocess.run(
            cmd,
            check=True,
            timeout=300,  # 5 minute timeout
            text=True
        )
        
        print(f"\n✓ {tcp_variant} completed successfully")
        time.sleep(1)
        return True
    except subprocess.TimeoutExpired:
        print(f"\n✗ {tcp_variant} timeout")
        return False
    except subprocess.CalledProcessError as e:
        print(f"\n✗ {tcp_variant} failed with exit code {e.returncode}")
        return False
    except Exception as e:
        print(f"\n✗ {tcp_variant} exception: {e}")
        return False

def parse_csv(filename):
    """Parse aggregate CSV results file"""
    try:
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            row = next(reader)
            return {
                'variant': row['TCP_Variant'],
                'total_throughput': float(row['Total_Throughput_Mbps']),
                'avg_throughput': float(row['Avg_Throughput_Per_Flow_Mbps']),
                'avg_delay': float(row['Avg_Delay_ms']),
                'total_lost': int(float(row['Total_Lost_Packets'])),
                'loss_rate': float(row['Loss_Rate_Percent']),
                'num_flows': int(float(row['Num_Flows']))
            }
    except Exception as e:
        print(f"Error parsing {filename}: {e}")
        return None

def parse_perflow_csv(filename):
    """Parse per-flow CSV results file"""
    try:
        flows = []
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                flows.append({
                    'flow_id': int(row['Flow_ID']),
                    'throughput': float(row['Throughput_Mbps']),
                    'delay': float(row['Delay_ms'])
                })
        return flows
    except Exception as e:
        print(f"Error parsing {filename}: {e}")
        return None

def generate_cwnd_plots(variants):
    """Generate congestion window plots - one image per variant"""
    if not HAS_MATPLOTLIB:
        print("Skipping cwnd plots (matplotlib not available)")
        return
    
    print("\nGenerating congestion window plots...")
    
    colors = ['#e74c3c', '#3498db', '#2ecc71']
    flow_labels = ['Flow 1 (1Mbps/50ms)', 'Flow 2 (2Mbps/25ms)', 'Flow 3 (3Mbps/16ms)']
    
    for variant in variants:
        # Create figure with 3 subplots for this variant
        fig, axes = plt.subplots(3, 1, figsize=(12, 10))
        fig.suptitle(f'Congestion Window Evolution: TCP {variant}',
                     fontsize=14, fontweight='bold')
        
        for flow_idx in range(3):
            ax = axes[flow_idx]
            cwnd_file = os.path.join(NS3_ROOT, OUTPUT_DIR, f"{variant}_cwnd_flow{flow_idx + 1}.csv")
            
            if os.path.exists(cwnd_file):
                try:
                    with open(cwnd_file, 'r') as f:
                        reader = csv.DictReader(f)
                        times = []
                        cwnds = []
                        
                        for row in reader:
                            times.append(float(row['Time']))
                            cwnds.append(int(row['CongestionWindow']) / 1400)  # Convert to segments
                        
                        if times and cwnds:
                            ax.plot(times, cwnds, label=f'TCP {variant}', 
                                   color=colors[flow_idx], linewidth=1.5, alpha=0.8)
                except Exception as e:
                    print(f"  Warning: Error reading {cwnd_file}: {e}")
            else:
                print(f"  Warning: {cwnd_file} not found")
            
            ax.set_ylabel('Congestion Window (segments)', fontsize=10, fontweight='bold')
            ax.set_title(flow_labels[flow_idx], fontsize=11, fontweight='bold')
            ax.legend(loc='best', fontsize=9)
            ax.grid(True, alpha=0.3)
            
            # Only show x-label on bottom plot
            if flow_idx == 2:
                ax.set_xlabel('Time (seconds)', fontsize=10, fontweight='bold')
        
        plt.tight_layout()
        
        # Save plot
        plot_file = os.path.join(NS3_ROOT, OUTPUT_DIR, f"TCP{variant}_cwnd_progress.png")
        plt.savefig(plot_file, dpi=300, bbox_inches='tight')
        print(f"✓ Saved CWND plot: {plot_file}")
        plt.close()

def generate_perflow_comparison_plots(perflow_data):
    """Generate per-flow throughput and delay comparison plots (only flows 1, 2, 3)"""
    if not HAS_MATPLOTLIB:
        print("Skipping per-flow plots (matplotlib not available)")
        return
    
    print("\nGenerating per-flow comparison plots...")
    
    # Prepare data - only use flows 1, 2, 3 (indices 0, 1, 2)
    variants = list(perflow_data.keys())
    flow_indices = [0, 1, 2]  # Flows 1, 2, 3
    flow_labels = ['Flow 1\n(1Mbps/50ms)', 'Flow 2\n(2Mbps/25ms)', 'Flow 3\n(3Mbps/16ms)']
    
    colors = {'LinuxReno': '#e74c3c', 'Fast': '#2ecc71'}
    x = np.arange(len(flow_indices))
    width = 0.35
    
    # Plot 1: Throughput comparison (separate image)
    fig1, ax1 = plt.subplots(figsize=(10, 6))
    fig1.suptitle('Throughput Comparison: TCP LinuxReno vs TCP FAST', 
                  fontsize=14, fontweight='bold')
    
    for i, variant in enumerate(variants):
        throughputs = [perflow_data[variant][idx]['throughput'] for idx in flow_indices]
        offset = width * (i - 0.5)
        bars = ax1.bar(x + offset, throughputs, width, label=f'TCP {variant}',
                       color=colors[variant], alpha=0.8, edgecolor='black', linewidth=1.5)
        
        # Add value labels on bars
        for bar in bars:
            height = bar.get_height()
            ax1.text(bar.get_x() + bar.get_width()/2., height,
                    f'{height:.3f}',
                    ha='center', va='bottom', fontsize=10, fontweight='bold')
    
    ax1.set_xlabel('Flow', fontsize=12, fontweight='bold')
    ax1.set_ylabel('Throughput (Mbps)', fontsize=12, fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels(flow_labels, fontsize=10)
    ax1.legend(fontsize=11, loc='upper right')
    ax1.grid(True, alpha=0.3, axis='y')
    
    plt.tight_layout()
    plot_file1 = os.path.join(NS3_ROOT, OUTPUT_DIR, "throughput_comparison_flows.png")
    plt.savefig(plot_file1, dpi=300, bbox_inches='tight')
    print(f"✓ Saved throughput comparison: {plot_file1}")
    plt.close()
    
    # Plot 2: Delay comparison (separate image)
    fig2, ax2 = plt.subplots(figsize=(10, 6))
    fig2.suptitle('Average Delay Comparison: TCP LinuxReno vs TCP FAST', 
                  fontsize=14, fontweight='bold')
    
    for i, variant in enumerate(variants):
        delays = [perflow_data[variant][idx]['delay'] for idx in flow_indices]
        offset = width * (i - 0.5)
        bars = ax2.bar(x + offset, delays, width, label=f'TCP {variant}',
                       color=colors[variant], alpha=0.8, edgecolor='black', linewidth=1.5)
        
        # Add value labels on bars
        for bar in bars:
            height = bar.get_height()
            ax2.text(bar.get_x() + bar.get_width()/2., height,
                    f'{height:.1f}',
                    ha='center', va='bottom', fontsize=10, fontweight='bold')
    
    ax2.set_xlabel('Flow', fontsize=12, fontweight='bold')
    ax2.set_ylabel('Average Delay (ms)', fontsize=12, fontweight='bold')
    ax2.set_xticks(x)
    ax2.set_xticklabels(flow_labels, fontsize=10)
    ax2.legend(fontsize=11, loc='upper right')
    ax2.grid(True, alpha=0.3, axis='y')
    
    plt.tight_layout()
    plot_file2 = os.path.join(NS3_ROOT, OUTPUT_DIR, "delay_comparison_flows.png")
    plt.savefig(plot_file2, dpi=300, bbox_inches='tight')
    print(f"✓ Saved delay comparison: {plot_file2}")
    plt.close()

def generate_aggregate_plots(results):
    """Generate aggregate comparison plots (3 plots: total throughput, avg delay, loss rate)"""
    if not HAS_MATPLOTLIB:
        print("Skipping aggregate plots (matplotlib not available)")
        return
    
    print("\nGenerating aggregate comparison plots...")
    
    # Extract data
    variants = []
    total_throughputs = []
    delays = []
    loss_rates = []
    
    for variant in TCP_VARIANTS:
        if variant in results:
            variants.append(variant)
            total_throughputs.append(results[variant]['total_throughput'])
            delays.append(results[variant]['avg_delay'])
            loss_rates.append(results[variant]['loss_rate'])
    
    if not variants:
        print("No data to plot")
        return
    
    # Create figure with 1x3 subplots
    fig, axes = plt.subplots(1, 3, figsize=(15, 5))
    fig.suptitle('TCP Variant Comparison - Aggregate Statistics',
                 fontsize=14, fontweight='bold')
    
    colors = {'LinuxReno': '#e74c3c', 'Fast': '#2ecc71'}
    bar_colors = [colors[v] for v in variants]
    x_pos = np.arange(len(variants))
    
    # Plot 1: Total Throughput
    ax1 = axes[0]
    bars1 = ax1.bar(x_pos, total_throughputs, color=bar_colors, alpha=0.8, edgecolor='black', linewidth=1.5)
    ax1.set_ylabel('Total Throughput (Mbps)', fontsize=11, fontweight='bold')
    ax1.set_title('Total Throughput', fontsize=12, fontweight='bold')
    ax1.set_xticks(x_pos)
    ax1.set_xticklabels([f'TCP {v}' for v in variants], fontsize=10)
    ax1.grid(True, alpha=0.3, axis='y')
    
    for bar, value in zip(bars1, total_throughputs):
        height = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2., height,
                f'{value:.2f}',
                ha='center', va='bottom', fontsize=10, fontweight='bold')
    
    # Plot 2: Average Delay
    ax2 = axes[1]
    bars2 = ax2.bar(x_pos, delays, color=bar_colors, alpha=0.8, edgecolor='black', linewidth=1.5)
    ax2.set_ylabel('Average Delay (ms)', fontsize=11, fontweight='bold')
    ax2.set_title('Average Delay', fontsize=12, fontweight='bold')
    ax2.set_xticks(x_pos)
    ax2.set_xticklabels([f'TCP {v}' for v in variants], fontsize=10)
    ax2.grid(True, alpha=0.3, axis='y')
    
    for bar, value in zip(bars2, delays):
        height = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., height,
                f'{value:.1f}',
                ha='center', va='bottom', fontsize=10, fontweight='bold')
    
    # Plot 3: Packet Loss Rate
    ax3 = axes[2]
    bars3 = ax3.bar(x_pos, loss_rates, color=bar_colors, alpha=0.8, edgecolor='black', linewidth=1.5)
    ax3.set_ylabel('Packet Loss Rate (%)', fontsize=11, fontweight='bold')
    ax3.set_title('Packet Loss Rate', fontsize=12, fontweight='bold')
    ax3.set_xticks(x_pos)
    ax3.set_xticklabels([f'TCP {v}' for v in variants], fontsize=10)
    ax3.grid(True, alpha=0.3, axis='y')
    
    for bar, value in zip(bars3, loss_rates):
        height = bar.get_height()
        ax3.text(bar.get_x() + bar.get_width()/2., height,
                f'{value:.2f}',
                ha='center', va='bottom', fontsize=10, fontweight='bold')
    
    plt.tight_layout()
    
    # Save plot
    plot_file = os.path.join(NS3_ROOT, OUTPUT_DIR, "tcp_comparison_aggregate.png")
    plt.savefig(plot_file, dpi=300, bbox_inches='tight')
    print(f"✓ Saved aggregate comparison: {plot_file}")
    plt.close()

def print_summary(results):
    """Print summary table"""
    print("\n" + "="*90)
    print("SIMULATION RESULTS SUMMARY")
    print("="*90)
    print("\nTopology: 1 server → 5 routers (6Mbps/100ms) → 15 receivers (heterogeneous)")
    print("  Router-Receiver links: 1Mbps/50ms, 2Mbps/25ms, 3Mbps/16ms")
    print("  Total flows: 45 (3 per receiver)")
    print("\n" + "-"*90)
    print(f"{'TCP Variant':<15} {'Total Tput':<15} {'Avg Tput/Flow':<18} {'Avg Delay':<12} {'Loss Rate'}")
    print(f"{'':15} {'(Mbps)':<15} {'(Mbps)':<18} {'(ms)':<12} {'(%)'}")
    print("-"*90)
    
    for variant in TCP_VARIANTS:
        if variant in results:
            r = results[variant]
            print(f"{variant:<15} {r['total_throughput']:<15.2f} {r['avg_throughput']:<18.4f} "
                  f"{r['avg_delay']:<12.1f} {r['loss_rate']:<.2f}")
    
    print("="*90)
    
    # Calculate comparisons
    if 'LinuxReno' in results and 'Fast' in results:
        print("\nFAST vs LinuxReno Comparison:")
        throughput_diff = ((results['Fast']['total_throughput'] - results['LinuxReno']['total_throughput']) / 
                          results['LinuxReno']['total_throughput'] * 100)
        delay_diff = ((results['Fast']['avg_delay'] - results['LinuxReno']['avg_delay']) / 
                     results['LinuxReno']['avg_delay'] * 100)
        loss_diff = ((results['Fast']['loss_rate'] - results['LinuxReno']['loss_rate']) / 
                    (results['LinuxReno']['loss_rate'] + 0.001) * 100)
        
        print(f"  Throughput: {throughput_diff:+.1f}%")
        print(f"  Delay: {delay_diff:+.1f}%")
        print(f"  Loss Rate: {loss_diff:+.1f}%")
    
    print()

def main():
    """Main execution"""
    import argparse
    
    parser = argparse.ArgumentParser(description='Run fanout topology TCP simulations')
    parser.add_argument('--skip-sim', action='store_true', help='Skip simulation, only plot existing results')
    args = parser.parse_args()
    
    print("="*90)
    print("FANOUT TOPOLOGY TCP COMPARISON STUDY")
    print("="*90)
    print(f"TCP Variants: {TCP_VARIANTS}")
    print(f"Simulation Time: {SIMULATION_TIME} seconds")
    print(f"Output Directory: {OUTPUT_DIR}")
    print("="*90)
    
    if not args.skip_sim:
        print("\nSTEP 1: Running Simulations")
        print("="*90)
        
        success_count = 0
        for variant in TCP_VARIANTS:
            if run_simulation(variant, SIMULATION_TIME):
                success_count += 1
        
        print("\n" + "="*90)
        print(f"Simulation Summary: {success_count}/{len(TCP_VARIANTS)} completed")
        print("="*90)
    
    print("\nSTEP 2: Loading Results")
    print("="*90)
    
    # Collect aggregate results
    results = {}
    for variant in TCP_VARIANTS:
        csv_file = os.path.join(NS3_ROOT, OUTPUT_DIR, f"{variant}_fanout.csv")
        if os.path.exists(csv_file):
            data = parse_csv(csv_file)
            if data:
                results[variant] = data
                print(f"✓ Loaded {variant} results")
        else:
            print(f"✗ Warning: {csv_file} not found")
    
    # Collect per-flow results
    perflow_data = {}
    for variant in TCP_VARIANTS:
        perflow_file = os.path.join(NS3_ROOT, OUTPUT_DIR, f"{variant}_perflow.csv")
        if os.path.exists(perflow_file):
            flows = parse_perflow_csv(perflow_file)
            if flows:
                perflow_data[variant] = flows
                print(f"✓ Loaded {variant} per-flow results ({len(flows)} flows)")
        else:
            print(f"✗ Warning: {perflow_file} not found")
    
    if not results:
        print("\n✗ No results found! Run simulations first.")
        return
    
    # Print summary
    print_summary(results)
    
    # Generate plots
    if HAS_MATPLOTLIB:
        print("\nSTEP 3: Generating Plots")
        print("="*90)
        
        generate_aggregate_plots(results)
        
        if perflow_data:
            generate_perflow_comparison_plots(perflow_data)
        
        generate_cwnd_plots(list(results.keys()))
    
    print("\n" + "="*90)
    print("✓ Done! Check results/og-sim-2/ for CSV files and plots")
    print("="*90)

if __name__ == "__main__":
    main()
