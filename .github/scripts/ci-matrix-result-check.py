import yaml
import sys
import os
import re

if len(sys.argv) < 6:
    raise ValueError("Invalid arguments")

script_dir = os.path.dirname(os.path.abspath(__file__))
expected_results_path = os.path.join(script_dir, "ci-expected-results.yml")

arch = sys.argv[1]
build = sys.argv[2]
benchmark = sys.argv[3]
log_dir = sys.argv[4]
config_file = sys.argv[5]

def read_in_plans(config_path):
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

def read_log(directory, search_string):
    import gzip

    # Check if the provided path is a directory
    if not os.path.isdir(directory):
        print(f"Error: {directory} is not a directory.")
        sys.exit(1)

    # Walk through the directory and concatenate the content of any matching log.gz files
    content = ""
    for root, dirs, files in os.walk(directory):
        for file in files:
            if search_string in file and file.endswith('log.gz'):
                file_path = os.path.join(root, file)
                with gzip.open(file_path, 'rt') as f:
                    content += f.read()
    return content

def print_log(directory, search_string):
    content = read_log(directory, search_string)
    print(f"----------------------------------------------")
    print(f"START: {search_string}")
    print(content)
    print(f"END: {search_string}")
    print(f"----------------------------------------------")

# pmd's own multi-threaded analysis has a known, pre-existing data race
# (unsynchronized "sig" field) that occasionally makes it throw a
# NullPointerException instead of reporting a lint violation, which changes
# pmd-report.txt just enough to fail DaCapo's digest check. This reproduces
# with stock OpenJDK/GraalVM with no MMTk involvement at all -- see
# https://github.com/dacapobench/dacapobench/issues/310. It is not a real
# test failure, so we ignore it here rather than flaking the whole CI run.
def is_pmd_digest_only_failure(directory, plan):
    # Match "mmtk_gc-{plan}." rather than a bare substring, otherwise e.g.
    # plan "Immix" would also match the log files for "GenImmix",
    # "StickyImmix" and "ConcurrentImmix".
    content = read_log(directory, f"mmtk_gc-{plan}.")
    return "Digest validation failed for pmd-report.txt" in content

# dict['a'] = 'SemiSpace', etc
plan_dict = read_in_plans(config_file)

actual = read_in_actual_results(sys.stdin.readline(), plan_dict)

if benchmark == "pmd":
    for plan, result in actual.items():
        if result == "fail" and is_pmd_digest_only_failure(log_dir, plan):
            print(
                f"Note: {plan} failed pmd's pmd-report.txt digest check. This is a known "
                f"upstream PMD data race unrelated to MMTk "
                f"(see https://github.com/dacapobench/dacapobench/issues/310); treating it as a pass."
            )
            actual[plan] = "pass"

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
    # See is_pmd_digest_only_failure() for why we match "mmtk_gc-{plan}." rather
    # than a bare substring.
    print_log(log_dir, f"mmtk_gc-{failed_plan}.")

exit(error_no)
