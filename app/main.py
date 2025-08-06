#!/usr/bin/env python3
import sys
import logging
import traceback
import signal
from PyQt5.QtWidgets import QApplication
from app_window import AppWindow

def handle_exception(exc_type, exc_value, exc_traceback):
    if issubclass(exc_type, KeyboardInterrupt):
        sys.__excepthook__(exc_type, exc_value, exc_traceback)
        return
    logging.error("Uncaught exception", exc_info=(exc_type, exc_value, exc_traceback))
    sys.exit(1) 



def main():
    # Logging konfigurieren (wird in GUI-Konsole angezeigt)
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(message)s"
    )

    # Qt-Anwendung initialisieren
    app = QApplication(sys.argv)
    win = AppWindow()
    win.show()

    # SIGINT (Ctrl+C) sauber an Qt weiterleiten
    signal.signal(signal.SIGINT, lambda *args: app.quit())

    # Starte Qt-Event-Loop
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
