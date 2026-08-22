#!/usr/bin/env python3
"""Fetch or import a small, reproducible NTSLF HW/LW reference sample.

This is a manual validation-data importer, not a runtime dependency. It stores
only the requested days' factual events and links back to the publisher's live
table.
"""

import argparse
import datetime as dt
import html.parser
import json
import re
import urllib.parse
import urllib.request


class TableParser(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.rows = []
        self._row = None
        self._cell = None

    def handle_starttag(self, tag, attrs):
        if tag == "tr":
            self._row = []
        elif tag == "td" and self._row is not None:
            self._cell = []

    def handle_data(self, data):
        if self._cell is not None:
            self._cell.append(data)

    def handle_endtag(self, tag):
        if tag == "td" and self._cell is not None:
            self._row.append(" ".join(self._cell))
            self._cell = None
        elif tag == "tr" and self._row is not None:
            self.rows.append(self._row)
            self._row = None


def fetch(port):
    page = "https://ntslf.org/tides/uk-network/tidepred?" + urllib.parse.urlencode(
        {"port": port}
    )
    request = urllib.request.Request(
        "https://ntslf.org/files/ntslf_php/tidepred.php?t=1",
        data=urllib.parse.urlencode({"port": port}).encode(),
        headers={"User-Agent": "Mozilla/5.0", "Referer": page},
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return page, response.read().decode("utf-8")


def prediction_days(html):
    parser = TableParser()
    parser.feed(html)
    current_date = None
    result = []
    for row in parser.rows:
        if not row:
            continue
        label = re.sub(r"(\d+)(st|nd|rd|th)", r"\1", " ".join(row[0].split()))
        full = re.search(r"(?:[A-Za-z]{3}\s+)?(\d{1,2})\s+([A-Za-z]{3})\s+(20\d\d)", label)
        short = re.search(r"(?:[A-Za-z]{3}\s+)?(\d{1,2})$", label)
        if full:
            current_date = dt.datetime.strptime(
                f"{full.group(1)} {full.group(2)} {full.group(3)}", "%d %b %Y"
            ).date()
        elif short and current_date:
            day_number = int(short.group(1))
            month = current_date.month
            year = current_date.year
            if day_number < current_date.day:
                month += 1
                if month == 13:
                    month = 1
                    year += 1
            current_date = dt.date(year, month, day_number)
        else:
            continue
        events = []
        for cell in row[1:]:
            match = re.search(r"(\d\d):(\d\d)\s+([+-]?\d+(?:\.\d+)?)m\s+([HL])", cell)
            if not match:
                continue
            time = dt.datetime(
                current_date.year,
                current_date.month,
                current_date.day,
                int(match.group(1)),
                int(match.group(2)),
                tzinfo=dt.timezone.utc,
            )
            events.append(
                {
                    "type": "high" if match.group(4) == "H" else "low",
                    "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ"),
                    "unix_seconds": int(time.timestamp()),
                    "height_m": float(match.group(3)),
                }
            )
        if events:
            result.append((current_date, events))
    if not result:
        raise RuntimeError("NTSLF response did not contain dated prediction rows")
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--station-id", required=True)
    parser.add_argument("--latitude", required=True, type=float)
    parser.add_argument("--longitude", required=True, type=float)
    parser.add_argument("--output", required=True)
    parser.add_argument("--input-html")
    parser.add_argument("--days", type=int, default=1)
    parser.add_argument("--skip-days", type=int, default=0)
    args = parser.parse_args()
    if args.days <= 0 or args.skip_days < 0:
        parser.error("--days must be positive and --skip-days cannot be negative")
    source_url = "https://ntslf.org/tides/uk-network/tidepred?" + urllib.parse.urlencode(
        {"port": args.port}
    )
    if args.input_html:
        with open(args.input_html, encoding="utf-8") as source:
            html = source.read()
    else:
        source_url, html = fetch(args.port)
    days = prediction_days(html)
    selected = days[args.skip_days : args.skip_days + args.days]
    if not selected:
        raise RuntimeError("requested date window is outside the NTSLF table")
    events = [event for _, day_events in selected for event in day_events]
    manifest = {
        "schema_version": 1,
        "station": {
            "id": args.station_id,
            "name": args.port,
            "latitude": args.latitude,
            "longitude": args.longitude,
        },
        "reference": {
            "kind": "published_astronomical_forecast",
            "publisher": "UK National Tidal and Sea Level Facility",
            "source_url": source_url,
            "source_title": f"NTSLF high and low water predictions for {args.port}",
            "time_basis": "UTC/GMT",
            "vertical_datum_id": "chart-datum",
            "vertical_datum_name": f"{args.port} Chart Datum",
            "comparison_mode": "event_time_and_tidal_range",
            "retrieved_utc": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "sample_start_date": selected[0][0].isoformat(),
            "sample_end_date": selected[-1][0].isoformat(),
            "redistribution_note": "Small factual verification sample; consult the linked live publication for the current full table.",
        },
        "events": events,
    }
    with open(args.output, "w", encoding="utf-8") as output:
        json.dump(manifest, output, indent=2)
        output.write("\n")


if __name__ == "__main__":
    main()
