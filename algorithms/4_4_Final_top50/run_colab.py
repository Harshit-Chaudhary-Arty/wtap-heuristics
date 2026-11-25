"""Build and run the C implementation, export XLSX, and download it in Colab."""

from pathlib import Path
import subprocess

import pandas as pd


ROOT = Path(__file__).resolve().parents[2]
INPUT_FILE = Path("/content/Instances_for_GA.txt")
CSV_PATH = Path("/content/WTAP_MMR_50_Percent_Global_Greedy_Results.csv")
EXCEL_PATH = Path("/content/WTAP_MMR_50_Percent_Global_Greedy_Results.xlsx")
BINARY = ROOT / "build" / "4_4_Final_top50"


def main():
    if not INPUT_FILE.exists():
        raise FileNotFoundError(
            f"Cannot find {INPUT_FILE}. Upload Instances_for_GA.txt to /content."
        )

    subprocess.run(
        ["make", "-C", str(ROOT), str(BINARY.relative_to(ROOT))],
        check=True,
    )
    subprocess.run(
        [str(BINARY), str(INPUT_FILE), str(CSV_PATH)],
        cwd=ROOT,
        check=True,
    )

    results = pd.read_csv(CSV_PATH)
    summary_columns = [
        "Instance",
        "Weapons",
        "Targets",
        "Initial Objective",
        "Final Objective",
        "Reduction (%)",
        "Runtime (seconds)",
        "Relocation Moves",
        "Swap Moves",
        "3-Cycle Moves",
    ]

    with pd.ExcelWriter(EXCEL_PATH, engine="openpyxl") as writer:
        results.to_excel(writer, sheet_name="All Results", index=False)
        results[summary_columns].to_excel(
            writer, sheet_name="Summary", index=False
        )

    print(f"\nExcel file saved successfully:\n{EXCEL_PATH}", flush=True)

    try:
        from google.colab import files

        files.download(str(EXCEL_PATH))
        print("Excel download requested.", flush=True)
    except ImportError:
        print(f"Not running in Google Colab. File saved at {EXCEL_PATH}")


if __name__ == "__main__":
    main()
