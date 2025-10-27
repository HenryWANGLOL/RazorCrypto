import pandas as pd

df = pd.read_csv("/root/RazorTrade/config/subcodes_20250730.csv")
print(len(df.columns.to_list()))