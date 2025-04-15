import yaml
import sys
import os
import re

if len(sys.argv) < 5:
    raise ValueError("Invalid arguments")

script_dir = os.path.dirname(os.path.abspath(__file__));
config_path = os.path.join(script_dir, "..", "configs", "base.yml")
expected_results_path = os.path.join(script_dir, "ci-expected-results.yml")

arch = sys.argv[1]
build = sys.argv[2]
benchmark = sys.argv[3]
log_dir = sys.argv[4]

def read_in_plans():
    # Load the YAML file
    with open(config_path, "r") as f:
        data = yaml.safe_load(f)

    # Extract the values from the "configs" field
    configs = data["configs"]

    # Define the dictionary to store the values
    results = {}

    pattern = r"mmtk_gc-(.+?)(\||$)"

    # Loop through each property in configs
    for i, prop in enumerate(configs):
        # Extract the value behind "mmtk_gc-"
        m = re.search(pattern, prop)
        if m:
            value = m.group(1)
        else:
            raise ValueError(f"Cannot find a plan string in {prop}")

        # Store the value in the dictionary
        key = chr(97+i)
        results[key] = value

    return results

def read_in_actual_results(line, plan_dict):
    # Read the input from stdin
    input_string = line.strip()

    # Extract the benchmark name and discard the rest
    benchmark_name = input_string.split()[0]
    input_string = input_string.removeprefix(benchmark_name)

    # Extract the strings from the input, like 0abcdef or 1a.c.ef
    pattern = r"(\d+[a-z\.]+)"
    matches = re.findall(pattern, input_string)

    # list[0] = "abcdef", list[1] = "a.cd.f", etc
    raw_results = list()
    for m in matches:
        print(m)
        index = int(m[0])
        result = m[1:]
        assert len(raw_results) == index
        raw_results.append(result)

    # Format the raw results into a dict
    # dict['SemiSpace'] = true/false
    result_dict = {}
    for s in raw_results:
        # Start with a
        key = 97
        for c in s:
            plan = plan_dict[chr(key)]
            key += 1
            success = (c != '.')
            if plan in result_dict:
                result_dict[plan] = result_dict[plan] and success
            else:
                result_dict[plan] = success

    # Rewrite True/False into pass/fail
    for key in result_dict.keys():
        if result_dict[key]:
            result_dict[key] = 'pass'
        else:
            result_dict[key] = 'fail'

    return result_dict

def read_in_expected_results(build, benchmark):
    # Load the YAML file
    with open(expected_results_path, "r") as f:
        data = yaml.safe_load(f)

    return data["results"][arch][build][benchmark]

def print_log(directory, search_string):
    import gzip

    # Check if the provided path is a directory
    if not os.path.isdir(directory):
        print(f"Error: {directory} is not a directory.")
        sys.exit(1)

    # Walk through the directory
    for root, dirs, files in os.walk(directory):
        for file in files:
            if search_string in file:
                file_path = os.path.join(root, file)
                # Check if the file has a .gz extension
                if file_path.endswith('log.gz'):
                    with gzip.open(file_path, 'rt') as f:
                        content = f.read()
                        print(f"----------------------------------------------")
                        print(f"START: {file_path}")
                        print(content)
                        print(f"END: {file_path}")
                        print(f"----------------------------------------------")

# dict['a'] = 'SemiSpace', etc
plan_dict = read_in_plans()

actual = read_in_actual_results(sys.stdin.readline(), plan_dict)
expected = read_in_expected_results(build, benchmark)

print("Expected:")
print(expected)
print("Actual:")
print(actual)

print("=====")

# Return code. If we ignore results, we may still return 0 (no error)
error_no = 0
# All the failed plans. As long as the run failed, we print the log for the plan, even if the result is ignored.
failed_plans = []

for plan in expected:
    if plan in actual:
        if actual[plan] == 'fail':
            failed_plans.append(plan)

        if expected[plan] == "ignore":
            print(f"Result for {plan} is ignored")
            continue

        if expected[plan] != actual[plan]:
            error_no = 1
            if expected[plan] == "pass":
                print(f"Expect {plan} to pass, but it failed.")
            else:
                print(f"Expect {plan} to fail, but it passed.")
                print(f"- If we have fixed a bug and expect the benchmark to run, please update ci-expected-results.yml")

print(f"\nPrint logs for all failed runs: {failed_plans}\n")

for failed_plan in failed_plans:
    print_log(log_dir, failed_plan)

exit(error_no)
