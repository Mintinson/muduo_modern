"""Interactive HTTP benchmark comparison using Streamlit and Plotly.

Usage:
    1. Update data_path dict to point to your benchmark CSV files.
    2. Run: streamlit run interactive_plot.py

The benchmark CSV columns: server, threads, concurrency, rps, avg_latency_ms, p99_latency_ms
- 'server' column is used as the framework label (e.g. chaoxi, muduo, asio)
"""

import streamlit as st
import pandas as pd
import plotly.express as px
import plotly.graph_objects as go
from plotly.subplots import make_subplots

# ============================================================================
# Point to your benchmark result CSV files
# ============================================================================
data_path = {
    "chaoxi": "results/http_bench_chaoxi_20260627_113741.csv",
    "muduo": "results/http_bench_muduo_20260627_113903.csv",
    # "asio":   "results/http_bench_asio_20260627_130000.csv",
}


@st.cache_data
def load_data():
    df_list = []
    for key, path in data_path.items():
        temp_df = pd.read_csv(path)
        # 'server' column from CSV takes precedence over dict key
        if "server" not in temp_df.columns or temp_df["server"].nunique() == 1:
            temp_df["framework"] = key
        else:
            temp_df["framework"] = temp_df["server"]
        df_list.append(temp_df)
    return pd.concat(df_list, ignore_index=True)


df = load_data()

# ============================================================================
# Sidebar — controls
# ============================================================================
st.set_page_config(page_title="HTTP Benchmark", layout="wide")
st.title("HTTP Server Benchmark — Interactive Comparison")

st.sidebar.header("Filters")
frameworks = sorted(df["framework"].unique())
selected_frameworks = st.sidebar.multiselect("Frameworks", frameworks, default=list(frameworks))

concurrency_list = sorted(df["concurrency"].unique())
threads_list = sorted(df["threads"].unique())

all_concurrencies = st.sidebar.checkbox("Compare all concurrencies", value=False)

if all_concurrencies:
    selected_threads = st.sidebar.selectbox("Fixed Threads", threads_list)
    selected_concurrency = None
else:
    selected_concurrency = st.sidebar.selectbox("Concurrency", concurrency_list)
    selected_threads = st.sidebar.selectbox("Threads", threads_list)

# ============================================================================
# Chart 1: RPS vs Concurrency
# ============================================================================
st.header("RPS vs Concurrency")

filtered = df[df["framework"].isin(selected_frameworks)]
filtered["threads_label"] = filtered["threads"].apply(lambda t: f"t={t}")

fig1 = px.line(
    filtered,
    x="concurrency",
    y="rps",
    color="framework",
    line_dash="threads_label",
    markers=True,
    title="Requests/sec vs Concurrent Connections",
    labels={
        "concurrency": "Concurrent Connections",
        "rps": "Requests/sec",
        "framework": "Framework",
        "threads_label": "Threads",
    },
)
fig1.update_xaxes(type="log")
st.plotly_chart(fig1)

# ============================================================================
# Chart 2: Latency vs Concurrency
# ============================================================================
st.header("Latency vs Concurrency")

fig2 = px.line(
    filtered,
    x="concurrency",
    y="avg_latency_ms",
    color="framework",
    line_dash="threads_label",
    markers=True,
    title="Avg Latency (ms) vs Concurrent Connections",
    labels={
        "concurrency": "Concurrent Connections",
        "avg_latency_ms": "Avg Latency (ms)",
        "framework": "Framework",
        "threads_label": "Threads",
    },
)
fig2.update_xaxes(type="log")
st.plotly_chart(fig2)

# ============================================================================
# Chart 3: RPS vs Threads
# ============================================================================
st.header("RPS vs Threads")

filtered["concurrency_label"] = filtered["concurrency"].apply(lambda c: f"c={c}")

fig3 = px.line(
    filtered,
    x="threads",
    y="rps",
    color="framework",
    line_dash="concurrency_label",
    markers=True,
    title="Requests/sec vs Worker Threads",
    labels={
        "threads": "Worker Threads",
        "rps": "Requests/sec",
        "framework": "Framework",
        "concurrency_label": "Concurrency",
    },
)
fig3.update_xaxes(type="category")
st.plotly_chart(fig3)

# ============================================================================
# Chart 4: Bar comparison at fixed (threads, concurrency)
# ============================================================================
st.header("Framework Comparison (Bar Chart)")

if all_concurrencies:
    compare_df = filtered[filtered["threads"] == selected_threads]
    title = f"RPS by Framework (threads={selected_threads})"
    fig4 = px.bar(
        compare_df,
        x="concurrency",
        y="rps",
        color="framework",
        barmode="group",
        title=title,
        labels={"rps": "Requests/sec", "concurrency": "Concurrency"},
        text_auto=".0f",
    )
else:
    compare_df = filtered[
        (filtered["threads"] == selected_threads)
        & (filtered["concurrency"] == selected_concurrency)
    ]
    title = f"RPS by Framework (threads={selected_threads}, c={selected_concurrency})"
    fig4 = px.bar(
        compare_df,
        x="framework",
        y="rps",
        color="framework",
        title=title,
        labels={"rps": "Requests/sec"},
        text_auto=".0f",
    )
st.plotly_chart(fig4)

# ============================================================================
# Chart 5: P99 Latency
# ============================================================================
st.header("P99 Latency")

fig5 = px.line(
    filtered,
    x="concurrency",
    y="p99_latency_ms",
    color="framework",
    line_dash="threads_label",
    markers=True,
    title="P99 Latency (ms) vs Concurrent Connections",
    labels={
        "concurrency": "Concurrent Connections",
        "p99_latency_ms": "P99 Latency (ms)",
        "framework": "Framework",
        "threads_label": "Threads",
    },
)
fig5.update_xaxes(type="log")
st.plotly_chart(fig5)

# ============================================================================
# Data table
# ============================================================================
st.header("Raw Data")
st.dataframe(
    filtered.sort_values(["framework", "threads", "concurrency"]),
    hide_index=True,
    column_config={
        "rps": st.column_config.NumberColumn("RPS", format="%.1f"),
        "avg_latency_ms": st.column_config.NumberColumn("Avg Lat (ms)", format="%.2f"),
        "p99_latency_ms": st.column_config.NumberColumn("P99 Lat (ms)", format="%.2f"),
    },
)
