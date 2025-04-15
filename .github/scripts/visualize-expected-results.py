import yaml
import sys


def convert_result_to_emoji(result):
    """Convert result to corresponding emoji."""
    conversion = {
        'pass': ':white_check_mark:',
        'fail': ':x:',
        'ignore': ':question:'
    }
    return conversion.get(result, result)


def yaml_to_markdown_table(file_path):
    # Load YAML data from the given file path
    with open(file_path, 'r') as f:
        data = yaml.safe_load(f)

    markdown_tables = ""

    # Process each platform separately
    for platform_name in data['results'].keys():

        # Dynamically extract configurations
        configurations = list(data['results'][platform_name].keys())

        # Dynamically extract plan names from the data
        sample_test = list(data['results'][platform_name][configurations[0]].keys())[0]
        plans = list(data['results'][platform_name][configurations[0]][sample_test].keys())

        # Define the header for the markdown table
        header = f"### {platform_name}\n\n"
        header += "| Test Name |"
        for plan in plans:
            for config in configurations:
                header += f" {plan} ({config}) |"
        header += "\n|" + "-----------|" * (len(plans) * len(configurations) + 1) + "\n"

        # Extract data and construct the table content
        table_content = ""
        for test_name in data['results'][platform_name][configurations[0]].keys():
            row = f"| {test_name} |"
            for plan in plans:
                for config in configurations:
                    result = data['results'][platform_name][config][test_name].get(plan, '')
                    row += f" {convert_result_to_emoji(result)} |"
            table_content += row + "\n"

        # Append the table for the current platform to the final result
        markdown_tables += header + table_content + "\n\n"

    return markdown_tables

# Load YAML from a given file path
file_path = sys.argv[1]

markdown_tables = yaml_to_markdown_table(file_path)
print(markdown_tables)
