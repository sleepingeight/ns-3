#!/usr/bin/env python3
"""
Script to run TCP variant simulations and compare results from multi-RTT bottleneck simulations
Generates plots for throughput, delay, and congestion window comparisons
"""

import csv
import sys
import os
import subprocess
import matplotlib.pyplot as plt
import numpy as np

# Get the ns-3 root directory (2 levels up from this script)
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
NS3_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..'))

def run_simulation(variant):
    """Run ns-3 simulation for a given TCP variant"""
    print(f"\n{'='*80}")
    print(f"Running simulation for {variant}...")
    print(f"{'='*80}\n")
    
    # Change to ns-3 root directory
    os.chdir(NS3_ROOT)
    
    # Run the simulation
    cmd = [
        './ns3', 'run',
        f'tcp-multi-rtt-bottleneck --tcpVariant={variant}'
    ]
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=False, text=True)
        print(f"\n {variant} simulation completed successfully!")
        return True
    except subprocess.CalledProcessError as e:
        print(f"\n✗ Error running {variant} simulation:")
        print(f"  {e}")
        return False

def read_results(variant):
    """Read results for a given TCP variant"""
    filename = os.path.join(NS3_ROOT, 'results', 'tcp-multi-rtt', f"{variant}_results.csv")
    
    if not os.path.exists(filename):
        print(f"Warning: {filename} not found")
        return None
    
    flows = []
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            flows.append({
                'flow_id': int(row['Flow_ID']),
                'rtt_ms': int(row['RTT_ms']),
                'throughput': float(row['Throughput_Mbps']),
                'data_mb': float(row['Data_Received_MB']),
                'avg_delay': float(row['Avg_Delay_ms']),
                'loss_rate': float(row['Loss_Rate_Percent'])
            })
    
    return flows

def read_cwnd_data(variant, flow_id):
    """Read congestion window data for a given variant and flow"""
    filename = os.path.join(NS3_ROOT, 'results', 'tcp-multi-rtt', 
                           f"{variant}_flow{flow_id}_cwnd.dat")
    
    if not os.path.exists(filename):
        print(f"Warning: {filename} not found")
        return None, None
    
    times = []
    cwnds = []
    
    with open(filename, 'r') as f:
        for line in f:
            if line.startswith('#'):
                continue
            parts = line.strip().split()
            if len(parts) == 2:
                times.append(float(parts[0]))
                cwnds.append(float(parts[1]))
    
    return np.array(times), np.array(cwnds)

def calculate_stats(flows):
    """Calculate aggregate statistics"""
    throughputs = [f['throughput'] for f in flows]
    loss_rates = [f['loss_rate'] for f in flows]
    
    total = sum(throughputs)
    avg = total / len(throughputs)
    min_tput = min(throughputs)
    max_tput = max(throughputs)
    avg_loss = sum(loss_rates) / len(loss_rates) if loss_rates else 0
    
    # Jain's Fairness Index
    sum_squared = sum(t * t for t in throughputs)
    fairness = (total * total) / (len(throughputs) * sum_squared)
    
    return {
        'total': total,
        'avg': avg,
        'min': min_tput,
        'max': max_tput,
        'fairness': fairness,
        'avg_loss': avg_loss
    }

def plot_throughput_comparison(all_results, output_file='throughput_comparison.png'):
    """Generate bar chart comparing throughput across flows and variants"""
    variants = list(all_results.keys())
    num_flows = len(all_results[variants[0]]['flows'])
    
    fig, ax = plt.subplots(figsize=(12, 6))
    
    x = np.arange(num_flows)
    width = 0.35
    
    # Get throughputs for each variant
    throughputs = {}
    rtt_labels = []
    for variant in variants:
        throughputs[variant] = [all_results[variant]['flows'][i]['throughput'] 
                                for i in range(num_flows)]
    
    rtt_labels = [f"{all_results[variants[0]]['flows'][i]['rtt_ms']}ms" 
                  for i in range(num_flows)]
    
    # Create bars
    for i, variant in enumerate(variants):
        offset = width * (i - len(variants)/2 + 0.5)
        bars = ax.bar(x + offset, throughputs[variant], width, label=variant)
        
        # Add value labels on bars
        for bar in bars:
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                    f'{height:.1f}',
                    ha='center', va='bottom', fontsize=9)
    
    ax.set_xlabel('Flow RTT', fontsize=12, fontweight='bold')
    ax.set_ylabel('Throughput (Mbps)', fontsize=12, fontweight='bold')
    ax.set_title('Throughput Comparison: TCP Variants Across Different RTTs', 
                 fontsize=14, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels(rtt_labels)
    ax.legend(fontsize=11)
    ax.grid(axis='y', alpha=0.3, linestyle='--')
    
    plt.tight_layout()
    output_path = os.path.join(NS3_ROOT, 'results', 'tcp-multi-rtt', output_file)
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f" Saved throughput comparison plot: {output_path}")
    plt.close()

def plot_delay_comparison(all_results, output_file='delay_comparison.png'):
    """Generate bar chart comparing average delay across flows and variants"""
    variants = list(all_results.keys())
    num_flows = len(all_results[variants[0]]['flows'])
    
    fig, ax = plt.subplots(figsize=(12, 6))
    
    x = np.arange(num_flows)
    width = 0.35
    
    # Get delays for each variant
    delays = {}
    rtt_labels = []
    for variant in variants:
        delays[variant] = [all_results[variant]['flows'][i]['avg_delay'] 
                          for i in range(num_flows)]
    
    rtt_labels = [f"{all_results[variants[0]]['flows'][i]['rtt_ms']}ms" 
                  for i in range(num_flows)]
    
    # Create bars
    for i, variant in enumerate(variants):
        offset = width * (i - len(variants)/2 + 0.5)
        bars = ax.bar(x + offset, delays[variant], width, label=variant)
        
        # Add value labels on bars
        for bar in bars:
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                    f'{height:.1f}',
                    ha='center', va='bottom', fontsize=9)
    
    ax.set_xlabel('Flow RTT', fontsize=12, fontweight='bold')
    ax.set_ylabel('Average Delay (ms)', fontsize=12, fontweight='bold')
    ax.set_title('Average Delay Comparison: TCP Variants Across Different RTTs', 
                 fontsize=14, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels(rtt_labels)
    ax.legend(fontsize=11)
    ax.grid(axis='y', alpha=0.3, linestyle='--')
    
    plt.tight_layout()
    output_path = os.path.join(NS3_ROOT, 'results', 'tcp-multi-rtt', output_file)
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f" Saved delay comparison plot: {output_path}")
    plt.close()

def plot_loss_comparison(all_results, output_file='loss_comparison.png'):
    """Generate bar chart comparing packet loss across flows and variants"""
    variants = list(all_results.keys())
    num_flows = len(all_results[variants[0]]['flows'])
    
    fig, ax = plt.subplots(figsize=(12, 6))
    
    x = np.arange(num_flows)
    width = 0.35
    
    # Get loss rates for each variant
    loss_rates = {}
    rtt_labels = []
    for variant in variants:
        loss_rates[variant] = [all_results[variant]['flows'][i]['loss_rate'] 
                               for i in range(num_flows)]
    
    rtt_labels = [f"{all_results[variants[0]]['flows'][i]['rtt_ms']}ms" 
                  for i in range(num_flows)]
    
    # Create bars
    for i, variant in enumerate(variants):
        offset = width * (i - len(variants)/2 + 0.5)
        bars = ax.bar(x + offset, loss_rates[variant], width, label=variant)
        
        # Add value labels on bars
        for bar in bars:
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                    f'{height:.2f}',
                    ha='center', va='bottom', fontsize=9)
    
    ax.set_xlabel('Flow RTT', fontsize=12, fontweight='bold')
    ax.set_ylabel('Packet Loss Rate (%)', fontsize=12, fontweight='bold')
    ax.set_title('Packet Loss Rate Comparison: TCP Variants Across Different RTTs', 
                 fontsize=14, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels(rtt_labels)
    ax.legend(fontsize=11)
    ax.grid(axis='y', alpha=0.3, linestyle='--')
    
    plt.tight_layout()
    output_path = os.path.join(NS3_ROOT, 'results', 'tcp-multi-rtt', output_file)
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Saved loss rate comparison plot: {output_path}")
    plt.close()

def plot_cwnd_progress(variant, num_flows, output_file):
    """Generate 4 subplots showing congestion window progress for each flow"""
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle(f'Congestion Window Progress: {variant}', 
                 fontsize=16, fontweight='bold')
    
    axes = axes.flatten()
    
    for flow_id in range(num_flows):
        times, cwnds = read_cwnd_data(variant, flow_id)
        
        if times is not None and len(times) > 0:
            ax = axes[flow_id]
            ax.plot(times, cwnds, linewidth=1.5, color=f'C{flow_id}')
            
            # Get RTT for title
            rtt = [50, 100, 150, 200][flow_id]  # Default RTT values
            
            ax.set_xlabel('Time (s)', fontsize=10, fontweight='bold')
            ax.set_ylabel('Congestion Window (segments)', fontsize=10, fontweight='bold')
            ax.set_title(f'Flow {flow_id} (RTT={rtt}ms)', fontsize=11, fontweight='bold')
            ax.grid(True, alpha=0.3, linestyle='--')
            
            # Add statistics
            if len(cwnds) > 10:
                avg_cwnd = np.mean(cwnds[int(len(cwnds)*0.1):])  # Exclude initial ramp-up
                max_cwnd = np.max(cwnds)
                ax.text(0.98, 0.95, f'Avg: {avg_cwnd:.1f}\nMax: {max_cwnd:.1f}',
                       transform=ax.transAxes, fontsize=9,
                       verticalalignment='top', horizontalalignment='right',
                       bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
        else:
            axes[flow_id].text(0.5, 0.5, 'No data available',
                              ha='center', va='center', fontsize=12)
    
    plt.tight_layout()
    output_path = os.path.join(NS3_ROOT, 'results', 'tcp-multi-rtt', output_file)
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f" Saved CWND progress plot: {output_path}")
    plt.close()

def main():
    variants = ['TcpLinuxReno', 'TcpFast']
    
    print("\n" + "="*80)
    print("TCP VARIANT COMPARISON - Multi-RTT Bottleneck Scenario")
    print("="*80)
    print("\nSTEP 1: Running Simulations")
    print("="*80)
    
    # Run simulations for each variant
    simulation_success = {}
    for variant in variants:
        success = run_simulation(variant)
        simulation_success[variant] = success
        if not success:
            print(f"\n✗ Failed to run simulation for {variant}")
    
    # Check if all simulations completed
    if not all(simulation_success.values()):
        print("\n✗ Some simulations failed. Exiting.")
        return 1
    
    print("\n" + "="*80)
    print("STEP 2: Loading Results and Generating Analysis")
    print("="*80)
    
    # Load results
    all_results = {}
    for variant in variants:
        flows = read_results(variant)
        if flows:
            all_results[variant] = {
                'flows': flows,
                'stats': calculate_stats(flows)
            }
    
    if not all_results:
        print("No results found. Check simulation output.")
        return 1
    
    # Print per-flow comparison
    print("\nPER-FLOW THROUGHPUT COMPARISON (Mbps):")
    print("-" * 80)
    
    # Get number of flows from first result
    num_flows = len(list(all_results.values())[0]['flows'])
    
    # Header
    header = f"{'RTT (ms)':<12}"
    for variant in variants:
        if variant in all_results:
            header += f"{variant:<20}"
    print(header)
    print("-" * 80)
    
    # Per-flow data
    for i in range(num_flows):
        rtt = list(all_results.values())[0]['flows'][i]['rtt_ms']
        line = f"{rtt:<12}"
        
        for variant in variants:
            if variant in all_results:
                throughput = all_results[variant]['flows'][i]['throughput']
                line += f"{throughput:<20.2f}"
        
        print(line)
    
    # Print loss rate comparison
    print("\nPER-FLOW PACKET LOSS RATE (%):")
    print("-" * 80)
    
    header = f"{'RTT (ms)':<12}"
    for variant in variants:
        if variant in all_results:
            header += f"{variant:<20}"
    print(header)
    print("-" * 80)
    
    for i in range(num_flows):
        rtt = list(all_results.values())[0]['flows'][i]['rtt_ms']
        line = f"{rtt:<12}"
        
        for variant in variants:
            if variant in all_results:
                loss = all_results[variant]['flows'][i]['loss_rate']
                line += f"{loss:<20.2f}"
        
        print(line)
    
    # Print delay comparison
    print("\nPER-FLOW AVERAGE DELAY COMPARISON (ms):")
    print("-" * 80)
    
    header = f"{'RTT (ms)':<12}"
    for variant in variants:
        if variant in all_results:
            header += f"{variant:<20}"
    print(header)
    print("-" * 80)
    
    for i in range(num_flows):
        rtt = list(all_results.values())[0]['flows'][i]['rtt_ms']
        line = f"{rtt:<12}"
        
        for variant in variants:
            if variant in all_results:
                delay = all_results[variant]['flows'][i]['avg_delay']
                line += f"{delay:<20.2f}"
        
        print(line)
    
    # Print aggregate statistics
    print("\n" + "="*80)
    print("AGGREGATE STATISTICS:")
    print("="*80)
    
    print(f"\n{'Metric':<25}", end="")
    for variant in variants:
        if variant in all_results:
            print(f"{variant:<20}", end="")
    print()
    print("-" * 80)
    
    metrics = [
        ('Total Throughput (Mbps)', 'total'),
        ('Average Throughput (Mbps)', 'avg'),
        ('Min Throughput (Mbps)', 'min'),
        ('Max Throughput (Mbps)', 'max'),
        ('Fairness Index (Jain)', 'fairness')
    ]
    
    for metric_name, metric_key in metrics:
        print(f"{metric_name:<25}", end="")
        for variant in variants:
            if variant in all_results:
                value = all_results[variant]['stats'][metric_key]
                if metric_key == 'fairness':
                    print(f"{value:<20.4f}", end="")
                else:
                    print(f"{value:<20.2f}", end="")
        print()
    
    # Analysis summary
    print("\n" + "="*80)
    print("ANALYSIS:")
    print("="*80)
    
    # Compare fairness
    fairness_scores = {v: all_results[v]['stats']['fairness'] for v in variants if v in all_results}
    best_fairness = max(fairness_scores.items(), key=lambda x: x[1])
    
    print(f"\n Best Fairness: {best_fairness[0]} (Jain Index: {best_fairness[1]:.4f})")
    
    # Compare total throughput
    total_throughputs = {v: all_results[v]['stats']['total'] for v in variants if v in all_results}
    best_throughput = max(total_throughputs.items(), key=lambda x: x[1])
    
    print(f" Best Total Throughput: {best_throughput[0]} ({best_throughput[1]:.2f} Mbps)")
    
    # RTT bias analysis
    print("\n RTT Bias (Low RTT flow throughput / High RTT flow throughput):")
    for variant in variants:
        if variant in all_results:
            flows = all_results[variant]['flows']
            low_rtt_tput = flows[0]['throughput']  # 50ms flow
            high_rtt_tput = flows[-1]['throughput']  # 200ms flow
            bias = low_rtt_tput / high_rtt_tput if high_rtt_tput > 0 else float('inf')
            print(f"  {variant}: {bias:.2f}x (50ms={low_rtt_tput:.2f} Mbps, 200ms={high_rtt_tput:.2f} Mbps)")
    
    # Generate plots
    print("\n" + "="*80)
    print("STEP 3: GENERATING PLOTS")
    print("="*80 + "\n")
    
    # 1. Throughput comparison bar chart
    plot_throughput_comparison(all_results, 'throughput_comparison.png')
    
    # 2. Delay comparison bar chart
    plot_delay_comparison(all_results, 'delay_comparison.png')
    
    # 3. Packet loss comparison bar chart
    plot_loss_comparison(all_results, 'loss_comparison.png')
    
    # 4. Congestion window progress plots (one per variant)
    for variant in variants:
        if variant in all_results:
            output_file = f'{variant}_cwnd_progress.png'
            plot_cwnd_progress(variant, num_flows, output_file)
    
    print("\n" + "="*80)
    print(" All simulations completed and plots generated successfully!")
    print("="*80)
    print("\nGenerated files:")
    print(f"  - {os.path.join(NS3_ROOT, 'results', 'tcp-multi-rtt', 'throughput_comparison.png')}")
    print(f"  - {os.path.join(NS3_ROOT, 'results', 'tcp-multi-rtt', 'delay_comparison.png')}")
    print(f"  - {os.path.join(NS3_ROOT, 'results', 'tcp-multi-rtt', 'TcpLinuxReno_cwnd_progress.png')}")
    print(f"  - {os.path.join(NS3_ROOT, 'results', 'tcp-multi-rtt', 'TcpFast_cwnd_progress.png')}")
    print("="*80 + "\n")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())

