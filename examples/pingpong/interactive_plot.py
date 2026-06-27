"""An Interactive throughput comparison between different frameworks using Streamlit and Plotly.

Usage:
    1. Change the data_path dictionary to point to your benchmark result CSV files.
    2. Run the script:
        streamlit run interactive_plot.py
"""

import streamlit as st
import pandas as pd
import plotly.express as px

data_path = {
    "chaoxi": "results/bench_20260626_171046_chaoxi.csv",
    "muduo": "results/bench_20260625_204423_muduo.csv",
    "asio": "results/bench_20260626_192117_asio.csv",
}


@st.cache_data
def load_data():
    df = []
    for key, path in data_path.items():
        temp_df = pd.read_csv(path)
        temp_df["framework"] = key
        df.append(temp_df)
    return pd.concat(df, ignore_index=True)


df = load_data()

st.title("benchmark Throughput Comparison")

st.sidebar.header("conditions")
sessions_list = sorted(df["sessions"].unique())
blocksize_list = sorted(df["blockSize"].unique())

selected_session = st.sidebar.selectbox("Session", sessions_list)
selected_blocksize = st.sidebar.selectbox("Block Size", blocksize_list)


filtered_df = df[(df["sessions"] == selected_session) & (df["blockSize"] == selected_blocksize)]

fig = px.line(
    filtered_df,
    x="threads",
    y="throughput_mbps",
    color="framework",
    markers=True,
    title=f"Comparison between Throughputs (Session: {selected_session}, BlockSize: {selected_blocksize})",
    labels={
        "threads": "Threads Number",
        "throughput_mbps": "Throughput (MB/s)",
        "framework": "Test Framework",
    },
)

fig.update_xaxes(type="category")

st.plotly_chart(fig)
