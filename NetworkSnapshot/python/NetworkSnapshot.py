import ctypes
import socket
import struct
import datetime
import sys
import time
import psutil
import requests
import subprocess
from ctypes.wintypes import DWORD
from collections import namedtuple
from tabulate import tabulate
import json
import csv
import logging

# Setup logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# Check if running on Windows
if not sys.platform.startswith('win'):
    logging.error("This script is only compatible with Windows.")
    sys.exit(1)

# Define necessary structures (from iphlpapi.h)
class MIB_TCPROW_OWNER_PID(ctypes.Structure):
    _fields_ = [
        ("dwState", DWORD),
        ("dwLocalAddr", DWORD),
        ("dwLocalPort", DWORD),
        ("dwRemoteAddr", DWORD),
        ("dwRemotePort", DWORD),
        ("dwOwningPid", DWORD)
    ]

class MIB_TCPTABLE_OWNER_PID(ctypes.Structure):
    _fields_ = [
        ("dwNumEntries", DWORD),
        ("table", MIB_TCPROW_OWNER_PID * 1)  # Placeholder for variable array
    ]

class MIB_UDPROW_OWNER_PID(ctypes.Structure):
    _fields_ = [
        ("dwLocalAddr", DWORD),
        ("dwLocalPort", DWORD),
        ("dwOwningPid", DWORD)
    ]

class MIB_UDPTABLE_OWNER_PID(ctypes.Structure):
    _fields_ = [
        ("dwNumEntries", DWORD),
        ("table", MIB_UDPROW_OWNER_PID * 1)  # Placeholder for variable array
    ]

# TCP states (from mibtcp.h)
TCP_STATE = {
    1: "CLOSED",
    2: "LISTEN",
    3: "SYN_SENT",
    4: "SYN_RCVD",
    5: "ESTABLISHED",
    6: "FIN_WAIT1",
    7: "FIN_WAIT2",
    8: "CLOSE_WAIT",
    9: "CLOSING",
    10: "LAST_ACK",
    11: "TIME_WAIT",
    12: "DELETE_TCB",
}

# Namedtuples for organized connection data
TCPConnection = namedtuple("TCPConnection", [
    "local_ip", "local_port", "remote_ip", "remote_port", "state",
    "pid", "process_name", "exe_path", "service", "country", "hostname"
])
UDPConnection = namedtuple("UDPConnection", [
    "local_ip", "local_port", "pid", "process_name", "exe_path", "service"
])

# Cache for IP-to-country lookups
country_cache = {}

def ip_to_string(ip_addr):
    """Converts a DWORD IP address to dotted-decimal string."""
    return socket.inet_ntoa(struct.pack("<I", ip_addr))

def get_process_info(pid):
    """Retrieve process name and executable path using psutil, including zombie check."""
    try:
        proc = psutil.Process(pid)
        if proc.status() == psutil.STATUS_ZOMBIE:
            return "Zombie Process", "N/A"
        return proc.name(), proc.exe()
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        return "N/A", "N/A"

def get_service_name(port, protocol):
    """Get service name for a given port and protocol."""
    try:
        return socket.getservbyport(port, protocol)
    except OSError:
        return "unknown"

def get_country(ip):
    """Get country for an IP using ip-api.com (with caching)."""
    if ip in ('0.0.0.0', '127.0.0.1', 'N/A'):
        return 'N/A'
    if ip in country_cache:
        return country_cache[ip]
    try:
        response = requests.get(f'http://ip-api.com/json/{ip}?fields=country', timeout=2)
        if response.status_code == 200:
            country = response.json().get('country', 'N/A')
            country_cache[ip] = country
            return country
        return 'N/A'
    except Exception:
        return 'N/A'

def get_hostname(ip):
    """Performs a reverse DNS lookup."""
    if ip in ('0.0.0.0', '127.0.0.1', 'N/A'):
        return 'N/A'
    try:
        return socket.gethostbyaddr(ip)[0]
    except socket.herror:
        return 'N/A'

def get_connections_from_netstat():
    """Retrieves connections from netstat, focusing on PIDs and IPs."""
    connections = {}
    try:
        result = subprocess.run(['netstat', '-ano'], capture_output=True, text=True, check=True)
        for line in result.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 5 and parts[0] in ('TCP', 'UDP'):
                try:
                    proto = parts[0]
                    local_addr_port = parts[1]
                    if proto == 'TCP':
                        remote_addr_port = parts[2]
                        state = parts[3]
                        pid = int(parts[4])
                    else:  # UDP
                        remote_addr_port = "N/A"
                        state = "N/A"
                        pid = int(parts[3])
                    local_ip, local_port = local_addr_port.rsplit(':', 1)
                    local_port = int(local_port)
                    if proto == 'TCP':
                        remote_ip, remote_port = remote_addr_port.rsplit(':', 1)
                        remote_port = int(remote_port)
                    else:
                        remote_ip = "N/A"
                        remote_port = "N/A"
                    key = (proto, local_ip, local_port, remote_ip, remote_port, pid)
                    connections[key] = state
                except (ValueError, IndexError):
                    continue
    except subprocess.CalledProcessError as e:
        logging.error(f"Error running netstat: {e}")
    return connections

def get_tcp_connections(iphlpapi):
    """Retrieve and format TCP connections with additional info."""
    logging.info("Retrieving TCP connections...")
    connections = []
    buf_size = DWORD(0)
    result = iphlpapi.GetExtendedTcpTable(None, ctypes.byref(buf_size), True, 2, 2, 0)
    if result not in (0, 122):
        raise ctypes.WinError(result)
    
    tcp_table = (ctypes.c_char * buf_size.value)()
    result = iphlpapi.GetExtendedTcpTable(tcp_table, ctypes.byref(buf_size), True, 2, 2, 0)
    if result != 0:
        raise ctypes.WinError(result)
    
    table_ptr = ctypes.cast(tcp_table, ctypes.POINTER(MIB_TCPTABLE_OWNER_PID))
    num_entries = table_ptr.contents.dwNumEntries
    if num_entries == 0:
        return connections

    RowArrayType = MIB_TCPROW_OWNER_PID * num_entries
    rows = RowArrayType.from_address(ctypes.addressof(table_ptr.contents.table))
    
    netstat_connections = get_connections_from_netstat()
    
    for row in rows:
        local_ip = ip_to_string(row.dwLocalAddr)
        local_port = socket.htons(row.dwLocalPort & 0xFFFF)
        remote_ip = ip_to_string(row.dwRemoteAddr)
        remote_port = socket.htons(row.dwRemotePort & 0xFFFF)
        state = TCP_STATE.get(row.dwState, f"UNKNOWN ({row.dwState})")
        pid = row.dwOwningPid
        process_name, exe_path = get_process_info(pid)
        service = get_service_name(local_port, 'tcp')
        country = get_country(remote_ip)
        hostname = get_hostname(remote_ip)
        
        key = ('TCP', local_ip, local_port, remote_ip, remote_port, pid)
        if key in netstat_connections:
            netstat_state = netstat_connections[key]
            if state.startswith("UNKNOWN"):
                state = netstat_state
        
        connections.append(TCPConnection(
            local_ip, local_port, remote_ip, remote_port, state,
            pid, process_name, exe_path, service, country, hostname
        ))
    logging.info(f"Found {len(connections)} TCP connections.")
    return connections

def get_udp_connections(iphlpapi):
    """Retrieve and format UDP connections with additional info."""
    logging.info("Retrieving UDP connections...")
    connections = []
    buf_size = DWORD(0)
    result = iphlpapi.GetExtendedUdpTable(None, ctypes.byref(buf_size), True, 2, 1, 0)
    if result not in (0, 122):
        raise ctypes.WinError(result)
    
    udp_table = (ctypes.c_char * buf_size.value)()
    result = iphlpapi.GetExtendedUdpTable(udp_table, ctypes.byref(buf_size), True, 2, 1, 0)
    if result != 0:
        raise ctypes.WinError(result)
    
    table_ptr = ctypes.cast(udp_table, ctypes.POINTER(MIB_UDPTABLE_OWNER_PID))
    num_entries = table_ptr.contents.dwNumEntries
    if num_entries == 0:
        return connections
    
    RowArrayType = MIB_UDPROW_OWNER_PID * num_entries
    rows = RowArrayType.from_address(ctypes.addressof(table_ptr.contents.table))
    
    netstat_connections = get_connections_from_netstat()
    
    for row in rows:
        local_ip = ip_to_string(row.dwLocalAddr)
        local_port = socket.htons(row.dwLocalPort & 0xFFFF)
        pid = row.dwOwningPid
        process_name, exe_path = get_process_info(pid)
        service = get_service_name(local_port, 'udp')
        
        key = ('UDP', local_ip, local_port, "N/A", "N/A", pid)
        if key in netstat_connections and process_name == "N/A":
            try:
                proc = psutil.Process(pid)
                process_name = proc.name()
                exe_path = proc.exe()
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
        
        connections.append(UDPConnection(
            local_ip, local_port, pid, process_name, exe_path, service
        ))
    logging.info(f"Found {len(connections)} UDP connections.")
    return connections

def generate_snapshot(tcp_connections, udp_connections, duration=None):
    """Generate formatted output using tabulate."""
    timestamp = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    output = f"Network Snapshot - {timestamp}\n\n"

    # TCP Table
    if tcp_connections:
        tcp_headers = ["Local Addr:Port", "Service", "->", "Remote Addr:Port", "Service", "Country", "State", "PID", "Process", "Hostname"]
        tcp_table = []
        for conn in tcp_connections:
            tcp_table.append([
                f"{conn.local_ip}:{conn.local_port}", conn.service, "->",
                f"{conn.remote_ip}:{conn.remote_port}", get_service_name(conn.remote_port, 'tcp') if conn.remote_port else 'unknown',
                conn.country, conn.state, conn.pid, conn.process_name, conn.hostname
            ])
        output += tabulate(tcp_table, headers=tcp_headers, tablefmt="grid") + "\n\n"
    else:
        output += "No TCP Connections Found\n\n"

    # UDP Table
    if udp_connections:
        udp_headers = ["Local Addr:Port", "Service", "PID", "Process"]
        udp_table = []
        for conn in udp_connections:
            udp_table.append([
                f"{conn.local_ip}:{conn.local_port}", conn.service, conn.pid, conn.process_name
            ])
        output += tabulate(udp_table, headers=udp_headers, tablefmt="grid") + "\n"
    else:
        output += "No UDP Connections Found\n"

    return output

def save_snapshot_json(tcp_connections, udp_connections, filename):
    """Saves the snapshot to a JSON file."""
    data = {
        'timestamp': datetime.datetime.now().isoformat(),
        'tcp_connections': [conn._asdict() for conn in tcp_connections],
        'udp_connections': [conn._asdict() for conn in udp_connections],
    }
    with open(filename, 'w') as f:
        json.dump(data, f, indent=4)
    logging.info(f"Snapshot JSON saved to {filename}")

def save_snapshot_csv(tcp_connections, udp_connections, filename):
    """Saves to CSV files for TCP and UDP."""
    if tcp_connections:
        with open(f"{filename}_tcp.csv", 'w', newline='') as csvfile:
            writer = csv.writer(csvfile)
            writer.writerow(tcp_connections[0]._fields)
            for conn in tcp_connections:
                writer.writerow(conn)
        logging.info(f"TCP snapshot CSV saved to {filename}_tcp.csv")
    if udp_connections:
        with open(f"{filename}_udp.csv", 'w', newline='') as csvfile:
            writer = csv.writer(csvfile)
            writer.writerow(udp_connections[0]._fields)
            for conn in udp_connections:
                writer.writerow(conn)
        logging.info(f"UDP snapshot CSV saved to {filename}_udp.csv")

def monitor_connections(duration=30):
    """Monitor connections over a specified duration and compute averages."""
    iphlpapi = ctypes.windll.iphlpapi
    start_time = time.time()
    logging.info(f"Monitoring network connections for {duration} seconds...")

    # Initial snapshot
    tcp_initial = get_tcp_connections(iphlpapi)
    udp_initial = get_udp_connections(iphlpapi)
    
    # Progress indicator during monitoring
    for i in range(duration):
        time.sleep(1)
        logging.info(f"{i+1} out of {duration} seconds elapsed")

    # Final snapshot
    tcp_final = get_tcp_connections(iphlpapi)
    udp_final = get_udp_connections(iphlpapi)
    
    logging.info("Final Snapshot:")
    snapshot = generate_snapshot(tcp_final, udp_final)
    print(snapshot)
    
    filename = f"network_snapshot_{duration}sec"
    with open(f"{filename}.txt", "w") as f:
        f.write(snapshot)
    save_snapshot_json(tcp_final, udp_final, f"{filename}.json")
    save_snapshot_csv(tcp_final, udp_final, filename)
    logging.info(f"Snapshot saved to {filename}.txt, {filename}.json, and CSV files")

if __name__ == "__main__":
    try:
        iphlpapi = ctypes.windll.iphlpapi
        logging.info("Taking initial network snapshot...")
        tcp_conns = get_tcp_connections(iphlpapi)
        udp_conns = get_udp_connections(iphlpapi)
        snapshot = generate_snapshot(tcp_conns, udp_conns)
        print(snapshot)
        filename = "network_snapshot"
        with open(f"{filename}.txt", "w") as f:
            f.write(snapshot)
        save_snapshot_json(tcp_conns, udp_conns, f"{filename}.json")
        save_snapshot_csv(tcp_conns, udp_conns, filename)
        logging.info(f"Snapshot saved to {filename}.txt, {filename}.json, and CSV files")
    except Exception as e:
        logging.error(f"Error: {e}")
        sys.exit(1)
