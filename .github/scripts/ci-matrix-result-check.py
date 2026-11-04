import yaml
import sys
import os
import re

# Usage: ci-matrix-result-check.py <build> <benchmark> <log dir> <config file>
# The expected results are read from `expected-results.yml` in the same directory as the config file.
if len(sys.argv) < 5:
    raise ValueError("Invalid arguments")

build = sys.argv[1]
benchmark = sys.argv[2]
log_dir = sys.argv[3]
config_file = sys.argv[4]

expected_results_path = os.path.join(os.path.dirname(os.path.abspath(config_file)), "expected-results.yml")

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

    return data["results"][build][benchmark]

def parse_expected_result(plan, value):
    # Returns (kind, patterns). `kind` is one of pass, fail, ignore or flaky.
    # `patterns` is only non-empty for flaky. See the comment at the top of expected-results.yml.
    if value in ("pass", "fail", "ignore"):
        return value, []
    if isinstance(value, dict) and list(value.keys()) == ["flaky"]:
        flaky = value["flaky"]
        if isinstance(flaky, dict) and list(flaky.keys()) == ["include"]:
            patterns = flaky["include"]
            if isinstance(patterns, str):
                patterns = [patterns]
            if isinstance(patterns, list) and len(patterns) > 0 and all(isinstance(p, str) for p in patterns):
                return "flaky", patterns
    raise ValueError(f"Invalid expected result for {plan}: {value}. "
                     "Expect pass, fail, ignore, or {flaky: {include: <pattern or list of patterns>}}")

def plan_log_files(directory, plan):
    # Check if the provided path is a directory
    if not os.path.isdir(directory):
        print(f"Error: {directory} is not a directory.")
        sys.exit(1)

    # Match "mmtk_gc-<plan>." rather than the bare plan name. Otherwise "Immix"
    # would also match the logs for GenImmix, StickyImmix and ConcurrentImmix.
    search_string = f"mmtk_gc-{plan}."
    log_files = []
    for root, dirs, files in os.walk(directory):
        for file in files:
            if search_string in file and file.endswith('log.gz'):
                log_files.append(os.path.join(root, file))
    return sorted(log_files)

def read_log(file_path):
    import gzip
    with gzip.open(file_path, 'rt', errors='replace') as f:
        return f.read()

def print_log(directory, plan):
    for file_path in plan_log_files(directory, plan):
        print(f"----------------------------------------------")
        print(f"START: {file_path}")
        print(read_log(file_path))
        print(f"END: {file_path}")
        print(f"----------------------------------------------")

def find_flaky_pattern(directory, plan, patterns):
    # Returns (pattern, matched line) for the first pattern found in the logs of the plan, or None.
    for file_path in plan_log_files(directory, plan):
        content = read_log(file_path)
        for pattern in patterns:
            m = re.search(pattern, content, re.MULTILINE)
            if m:
                line_start = content.rfind("\n", 0, m.start()) + 1
                line_end = content.find("\n", m.end())
                line = content[line_start:line_end if line_end != -1 else len(content)]
                return pattern, line.strip()
    return None

# dict['a'] = 'SemiSpace', etc
plan_dict = read_in_plans(config_file)

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
    kind, patterns = parse_expected_result(plan, expected[plan])
    if plan in actual:
        if actual[plan] == 'fail':
            failed_plans.append(plan)

        if kind == "ignore":
            print(f"Result for {plan} is ignored")
            continue

        if kind == "flaky":
            if actual[plan] == "pass":
                continue
            found = find_flaky_pattern(log_dir, plan, patterns)
            if found:
                print(f"{plan} failed with a known flaky failure. Matched pattern {found[0]!r}: {found[1]}")
            else:
                error_no = 1
                print(f"Expect {plan} to pass, or to fail with one of the known patterns {patterns}, but it failed without matching any of them.")
            continue

        if kind != actual[plan]:
            error_no = 1
            if kind == "pass":
                print(f"Expect {plan} to pass, but it failed.")
            else:
                print(f"Expect {plan} to fail, but it passed.")
                print(f"- If we have fixed a bug and expect the benchmark to run, please update {expected_results_path}")

print(f"\nPrint logs for all failed runs: {failed_plans}\n")

for failed_plan in failed_plans:
    print_log(log_dir, failed_plan)

exit(error_no)
