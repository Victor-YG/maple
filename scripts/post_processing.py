import os
import argparse
import matplotlib.pyplot as plt


class core_state:
    def __init__(self, data_string):
        data_string = data_string.replace("csv: ", "").strip()
        data = [int(d) for d in data_string.split(",")]

        self.cycle = data[0]
        self.load_fifo_utilization = data[1]
        self.store_fifo_utilization = data[2]
        self.access_thread_pc = data[3]
        self.execute_thread_pc = data[4]


def parse_pc_list(filename):
    '''read program counter to ignore from file'''

    pc_to_ignore = set()
    with open(filename, "r") as f:
        lines = f.readlines()

    for line in lines:
        pc_to_ignore.add(int(line.strip()))

    return pc_to_ignore


def process_log_file(filename):
    '''read execution data from log file'''

    exec_data = []
    utilization_curr = None

    with open(filename, "r") as f:
        lines = f.readlines()

    for line in lines:
        if "csv:" in line:
            exec_data.append(core_state(line))

    return exec_data


def plot_core_stalls(filename, data, access_pc_to_ignore, execute_pc_to_ignore, stall_thres):
    '''Plot core utilization and highlight region of core stall'''

    cycles = []
    utilization_load_fifo = []
    utilization_store_fifo = []
    stalls_access = []
    stalls_execute = []
    access_stalled_at = []
    execute_stalled_at = []

    for state in data:
        cycles.append(state.cycle)
        utilization_load_fifo.append(state.load_fifo_utilization)
        utilization_store_fifo.append(state.store_fifo_utilization)

        if state.load_fifo_utilization >= stall_thres and \
           state.access_thread_pc not in access_pc_to_ignore:
            access_stalled_at.append(True)
        else:
            access_stalled_at.append(False)

        if state.store_fifo_utilization >= stall_thres and \
           state.execute_thread_pc not in access_pc_to_ignore:
            execute_stalled_at.append(True)
        else:
            execute_stalled_at.append(False)

    plt.figure(1)
    # access thread
    ax_1 = plt.subplot(211)
    ax_1.set_title("Load FIFO Utilization")
    plt.plot(cycles, utilization_load_fifo)
    plt.fill_between(cycles, y1=utilization_load_fifo, y2=[0 for i in range(len(cycles))], where=access_stalled_at)
    # execute thread
    ax_2 = plt.subplot(212)
    ax_2.set_title("Store FIFO Utilization")
    plt.plot(cycles, utilization_store_fifo)
    plt.fill_between(cycles, y1=utilization_store_fifo, y2=[0 for i in range(len(cycles))], where=execute_stalled_at)
    plt.savefig(os.path.join(os.curdir, filename))
    plt.show()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", help="Input log file.", required=True)
    parser.add_argument("--pc_access", help="Text file indicating on which PC access thread's stalls can be ignored.", required=False)
    parser.add_argument("--pc_execute", help="Text file indicating on which PC load thread's stalls can be ignored.", required=False)
    parser.add_argument("--fifo_size", help="Size of the fifo queue for load and store.", type=int, default=64, required=False)
    parser.add_argument("--output", help="File name for output plot.", default="plot.png", required=False)
    args = parser.parse_args()

    # check input file
    if os.path.exists(args.input) == False: exit("Input file '{}' doesn't exist.".format(args.input))

    # parse pc file
    access_pc_to_ignore = set()
    if args.pc_access != None:
        if os.path.exists(args.pc_access) == True:
            access_pc_to_ignore = parse_pc_list(args.pc_access)
        else: exit("PC file '{}' for access thread doesn't exist.".format(args.pc_access))
    execute_pc_to_ignore = set()
    if args.pc_execute != None:
        if os.path.exists(args.pc_execute) == True:
            execute_pc_to_ignore = parse_pc_list(args.pc_execute)
        else: exit("PC file '{}' for execute thread doesn't exist.".format(args.pc_execute))

    # parse logged information
    data = process_log_file(args.input)

    # plot stalls
    plot_core_stalls(args.output, data, access_pc_to_ignore, execute_pc_to_ignore, args.fifo_size)


if __name__ == "__main__":
    main()