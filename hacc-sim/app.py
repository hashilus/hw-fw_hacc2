import threading
import queue
import signal
import sys

from sim.state import SimulatorState
from sim.command_processor import CommandProcessor
from sim.udp_server import UDPServer
from sim.gui import SimulatorGUI
from sim.persist import load_all, save_all


def main() -> int:
    log_queue: "queue.Queue[str]" = queue.Queue()
    response_queue: "queue.Queue[tuple[bytes, tuple[str, int]]]" = queue.Queue()

    state = SimulatorState()
    # load persisted data (best-effort)
    load_all(state)
    processor = CommandProcessor(state=state, log_queue=log_queue)
    # Start GUI (main thread)
    gui = SimulatorGUI(state=state, processor=processor, log_queue=log_queue)
    try:
        gui.run()
    finally:
        # save on exit
        save_all(state)

    return 0


if __name__ == "__main__":
    # Graceful Ctrl+C on Windows console
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    sys.exit(main())


