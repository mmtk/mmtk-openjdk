import os
import sys
import yaml


def convert_result_to_emoji(result):
    """Convert result to corresponding emoji."""
    conversion = {
        'pass': ':white_check_mark:',
        'fail': ':x:',
        'ignore': ':question:'
    }
    return conversion.get(result, result)


def yaml_to_markdown_table(file_paths):
    markdown_tables = ""

    # Process each platform separately.
    # Each file is `.github/configs/<platform>/expected-results.yml`.
    for file_path in file_paths:
        platform_name = os.path.basename(os.path.dirname(os.path.abspath(file_path)))
        with open(file_path, 'r') as f:
            results = yaml.safe_load(f)['results']

        # Dynamically extract configurations
        configurations = list(results.keys())

        # Dynamically extract plan names from the data
        sample_test = list(results[configurations[0]].keys())[0]
        plans = list(results[configurations[0]][sample_test].keys())

        # Define the header for the markdown table
        header = f"### {platform_name}\n\n"
        header += "| Test Name |"
        for plan in plans:
            for config in configurations:
                header += f" {plan} ({config}) |"
        header += "\n|" + "-----------|" * (len(plans) * len(configurations) + 1) + "\n"

        # Extract data and construct the table content
        table_content = ""
        for test_name in results[configurations[0]].keys():
            row = f"| {test_name} |"
            for plan in plans:
                for config in configurations:
                    result = results[config][test_name].get(plan, '')
                    row += f" {convert_result_to_emoji(result)} |"
            table_content += row + "\n"

        # Append the table for the current platform to the final result
        markdown_tables += header + table_content + "\n\n"

    return markdown_tables

# Usage: visualize-expected-results.py <expected-results.yml>...
# e.g. visualize-expected-results.py .github/configs/*/expected-results.yml
markdown_tables = yaml_to_markdown_table(sys.argv[1:])
print(markdown_tables)
