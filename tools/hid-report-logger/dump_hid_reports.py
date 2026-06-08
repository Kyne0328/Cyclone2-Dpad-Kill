import hid
import time


def fmt_report(data):
    return " ".join(f"{b:02X}" for b in data)


def main():
    devices = hid.enumerate()

    print("HID devices:")
    for i, d in enumerate(devices):
        vid = d.get("vendor_id", 0)
        pid = d.get("product_id", 0)
        manufacturer = d.get("manufacturer_string") or ""
        product = d.get("product_string") or ""
        usage_page = d.get("usage_page", 0)
        usage = d.get("usage", 0)
        interface = d.get("interface_number", -1)

        print(
            f"[{i}] VID={vid:04X} PID={pid:04X} "
            f"UsagePage={usage_page:04X} Usage={usage:04X} "
            f"Interface={interface} "
            f"{manufacturer} {product}"
        )

    choice = int(input("\nSelect controller HID index: "))
    devinfo = devices[choice]

    device = hid.device()
    device.open_path(devinfo["path"])
    device.set_nonblocking(True)

    print("\nReading reports.")
    print("Leave the controller neutral first.")
    print("Then press ONLY D-pad Up, Down, Left, Right one at a time.")
    print("Press Ctrl+C to stop.\n")

    last = None

    try:
        while True:
            data = device.read(128)
            if data:
                report = bytes(data)

                if report != last:
                    if last is None:
                        print(f"REPORT len={len(report):02d}: {fmt_report(report)}")
                    else:
                        changes = []
                        for idx, (a, b) in enumerate(zip(last, report)):
                            if a != b:
                                changes.append(f"byte[{idx}]: {a:02X}->{b:02X}")

                        print(f"REPORT len={len(report):02d}: {fmt_report(report)}")
                        if changes:
                            print("CHANGED:", ", ".join(changes))
                        else:
                            print("CHANGED: length or unseen byte change")

                    last = report

            time.sleep(0.01)

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        device.close()


if __name__ == "__main__":
    main()
