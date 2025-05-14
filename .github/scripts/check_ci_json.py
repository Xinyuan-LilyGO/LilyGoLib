#!/usr/bin/env python3
import json
import os
import argparse
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description='Checks the ci.json file and determines if a build should be done')
    parser.add_argument('--dir-path', required=True, help='Path to the directory containing ci.json')
    parser.add_argument('--board', required=True, help='Type of board to check')
    parser.add_argument('--default-build', default='true', choices=['true', 'false'], help='Default should_build Value')
    parser.add_argument('--default-radio', default='Radio_SX1262', help='Default active_radio value')
    args = parser.parse_args()

    dir_path = Path(args.dir_path)
    json_path = dir_path / 'ci.json'
    board = args.board
    default_build = args.default_build.lower() == 'true'
    default_radio = args.default_radio

    print(f"Check the path: {dir_path}")
    print(f"CI JSON path: {json_path}")
    print(f"Board: {board}")
    print(f"Default build: {default_build}")
    print(f"Default radio: {default_radio}")

    should_build = default_build
    active_radio = default_radio

    if json_path.exists():
        try:
            with open(json_path, 'r') as f:
                data = json.load(f)
                targets = data.get('targets', {})
                
                if board in targets:
                    target_value = targets[board]
                    if isinstance(target_value, bool):
                        should_build = target_value
                        print(f"Find the configuration: {board} = {should_build}")
                    else:
                        print(f"The configuration of {board} is not a boolean value, so the default value is used: {should_build}")
                else:
                    print(f"Configuration not found for {board}, using default: {should_build}")
                
                file_active_radio = data.get('active_radio')
                if file_active_radio and isinstance(file_active_radio, str):
                    active_radio = file_active_radio
                    print(f"Find active_radio: {active_radio}")
                else:
                    print(f"active_radio field not found or value invalid, using default value: {active_radio}")
        except Exception as e:
            print(f"Error parsing JSON: {e}")
            print(f"Using the default configuration: build={should_build}, radio={active_radio}")

    print(f"::set-output name=should_build::{str(should_build).lower()}")
    print(f"::set-output name=active_radio::{active_radio}")

if __name__ == "__main__":
    main()    