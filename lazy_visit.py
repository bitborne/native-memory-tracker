#!/usr/bin/env python3
"""
Lazy Visit - Memory Analysis Visualization Tool
Analyzes mem_reg.log and mem_visit.log to visualize memory allocation and access patterns
"""

import streamlit as st
import pandas as pd
import numpy as np
import plotly.graph_objects as go
from elftools.elf.elffile import ELFFile
from dataclasses import dataclass, field
from typing import Dict, List, Optional
from collections import defaultdict

st.set_page_config(
    page_title="Lazy Visit - 内存分析工具",
    layout="wide",
    initial_sidebar_state="collapsed"
)

# 自定义 CSS：固定热力图容器、紧凑菜单栏
st.markdown("""
<style>
    /* 紧凑顶部菜单栏 */
    .top-menu {
        background-color: #f0f2f6;
        padding: 10px 15px;
        border-radius: 8px;
        margin-bottom: 10px;
    }

    /* 固定大小的热力图容器 */
    .heatmap-container {
        height: 500px;
        overflow: auto;
        border: 1px solid #ddd;
        border-radius: 8px;
        background-color: white;
    }

    /* 调整上传按钮样式 */
    .stUploadButton > button {
        font-size: 12px;
        padding: 4px 12px;
    }

    /* 紧凑的文件上传器 */
    .compact-uploader {
        margin-bottom: 0 !important;
    }

    /* 隐藏默认的文件上传标签 */
    .stFileUploader label {
        font-size: 12px !important;
        margin-bottom: 2px !important;
    }

    /* 详情面板样式 */
    .detail-panel {
        background-color: #f8f9fa;
        padding: 15px;
        border-radius: 8px;
        border: 1px solid #e9ecef;
    }
</style>
""", unsafe_allow_html=True)

@dataclass
class MemBlock:
    addr: int
    size: int
    actual_size: int
    alloc_time: int
    free_time: Optional[int] = None
    alloc_type: str = "malloc"
    tid: int = 0
    access_count: int = 0
    accessed_pages: int = 0
    total_pages: int = 0
    page_access_rate: float = 0.0
    last_access_time: Optional[int] = None
    first_access_time: Optional[int] = None
    category: str = ""
    callstack: List[int] = field(default_factory=list)

    @property
    def is_freed(self) -> bool:
        return self.free_time is not None

    @property
    def lifetime_us(self) -> int:
        if self.free_time:
            return self.free_time - self.alloc_time
        return 0

    @property
    def end_addr(self) -> int:
        return self.addr + self.size

    def calc_cold_hot_score(self, total_sequences: int) -> float:
        if total_sequences == 0 or self.total_pages == 0:
            return 0.0
        space_score = self.page_access_rate
        time_score = self.access_count / total_sequences
        return (space_score * 0.6 + time_score * 0.4)

@dataclass
class PageVisit:
    vaddr: int
    sequence: int
    timestamp: int
    pfn: int
    accessed: int
    region_name: str = ""

class ElfSymbolResolver:
    def __init__(self, elf_path: str):
        self.symbols = []
        self.load_base = 0  # 加载基址
        self.load_symbols(elf_path)

    def load_symbols(self, elf_path: str):
        try:
            with open(elf_path, 'rb') as f:
                elffile = ELFFile(f)

                # 计算加载基址（第一个 PT_LOAD 段的虚拟地址）
                for segment in elffile.iter_segments():
                    if segment['p_type'] == 'PT_LOAD':
                        self.load_base = segment['p_vaddr']
                        break

                # 加载符号表
                for section_name in ['.dynsym', '.symtab']:
                    section = elffile.get_section_by_name(section_name)
                    if section:
                        for symbol in section.iter_symbols():
                            if symbol['st_value'] != 0 and symbol['st_size'] > 0:
                                self.symbols.append({
                                    'name': symbol.name,
                                    'addr': symbol['st_value'],  # 相对地址
                                    'size': symbol['st_size']
                                })
                        if self.symbols:  # 优先使用第一个找到的符号表
                            break

                self.symbols.sort(key=lambda x: x['addr'])
                st.info(f"加载了 {len(self.symbols)} 个符号，基址: 0x{self.load_base:x}")

        except Exception as e:
            st.warning(f"加载ELF符号表失败: {e}")

    def resolve_address(self, addr: int) -> str:
        if not self.symbols:
            return f"0x{addr:x}"

        # 将绝对地址转换为相对地址
        # 注意：如果 addr 小于 load_base，可能已经是相对地址
        if addr >= self.load_base:
            rel_addr = addr - self.load_base
        else:
            rel_addr = addr

        # 二分查找符号
        left, right = 0, len(self.symbols)
        while left < right:
            mid = (left + right) // 2
            if self.symbols[mid]['addr'] <= rel_addr:
                left = mid + 1
            else:
                right = mid

        if left > 0:
            sym = self.symbols[left - 1]
            offset = rel_addr - sym['addr']
            if offset < sym['size'] or sym['size'] == 0:
                return f"{sym['name']}+0x{offset:x}" if offset else sym['name']

        return f"0x{addr:x}"

class MemRegParser:
    ALLOC_TYPES = {1: 'malloc', 2: 'realloc', 3: 'calloc', 4: 'free',
                   5: 'mmap', 6: 'munmap', 7: 'mmap64',
                   8: 'posix_memalign', 9: 'aligned_alloc'}

    def __init__(self, log_path: str):
        self.blocks: Dict[int, MemBlock] = {}
        self.parse(log_path)

    def parse(self, log_path: str):
        pending_allocs: Dict[int, dict] = {}

        with open(log_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue

                try:
                    parts = line.split(',')
                    if len(parts) < 9:
                        continue

                    timestamp = int(parts[0])
                    alloc_type = int(parts[1])
                    addr = int(parts[2], 16) if parts[2].startswith('0x') else int(parts[2])
                    req_size = int(parts[3])
                    actual_size = int(parts[4])
                    tid = int(parts[5])

                    callstack = []
                    for i in range(8, min(13, len(parts))):
                        if parts[i] and parts[i] != '0':
                            try:
                                pc = int(parts[i], 16) if parts[i].startswith('0x') else int(parts[i])
                                if pc != 0:
                                    callstack.append(pc)
                            except:
                                pass

                    if alloc_type in [1, 2, 3, 5, 7, 8, 9]:  # Alloc
                        pending_allocs[addr] = {
                            'timestamp': timestamp,
                            'type': alloc_type,
                            'addr': addr,
                            'size': req_size,
                            'actual_size': actual_size,
                            'tid': tid,
                            'callstack': callstack
                        }
                    elif alloc_type in [4, 6]:  # Free
                        if addr in pending_allocs:
                            alloc = pending_allocs.pop(addr)
                            block = MemBlock(
                                addr=alloc['addr'],
                                size=alloc['size'],
                                actual_size=alloc['actual_size'],
                                alloc_time=alloc['timestamp'],
                                free_time=timestamp,
                                alloc_type=self.ALLOC_TYPES.get(alloc['type'], 'unknown'),
                                tid=alloc['tid'],
                                callstack=alloc['callstack']
                            )
                            self.blocks[addr] = block

                except Exception as e:
                    continue

        for addr, alloc in pending_allocs.items():
            block = MemBlock(
                addr=alloc['addr'],
                size=alloc['size'],
                actual_size=alloc['actual_size'],
                alloc_time=alloc['timestamp'],
                alloc_type=self.ALLOC_TYPES.get(alloc['type'], 'unknown'),
                tid=alloc['tid'],
                callstack=alloc['callstack']
            )
            self.blocks[addr] = block

class MemVisitParser:
    def __init__(self, log_path: str):
        self.visits: List[PageVisit] = []
        self.sequences: set = set()
        self.parse(log_path)

    def parse(self, log_path: str):
        with open(log_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue

                try:
                    parts = line.split(',')
                    if len(parts) < 5:
                        continue

                    timestamp = int(parts[0])
                    sequence = int(parts[1])
                    vaddr = int(parts[2], 16) if parts[2].startswith('0x') else int(parts[2])
                    pfn = int(parts[3])
                    accessed = int(parts[4])
                    region_name = parts[5].strip('()') if len(parts) > 5 else ""

                    visit = PageVisit(
                        vaddr=vaddr,
                        sequence=sequence,
                        timestamp=timestamp,
                        pfn=pfn,
                        accessed=accessed,
                        region_name=region_name
                    )
                    self.visits.append(visit)
                    self.sequences.add(sequence)

                except Exception as e:
                    continue

        self.sequences = sorted(self.sequences)

def format_time(us: int) -> str:
    seconds = us // 1000000
    minutes = seconds // 60
    hours = minutes // 60
    if hours > 0:
        return f"{hours}:{minutes % 60:02d}:{seconds % 60:02d}"
    elif minutes > 0:
        return f"{minutes}:{seconds % 60:02d}.{ (us // 1000) % 1000:03d}"
    else:
        return f"{seconds}.{ (us // 1000) % 1000:03d}s"

def format_size(size: int) -> str:
    if size >= 1024 * 1024 * 1024:
        return f"{size / (1024 * 1024 * 1024):.2f} GB"
    elif size >= 1024 * 1024:
        return f"{size / (1024 * 1024):.2f} MB"
    elif size >= 1024:
        return f"{size / 1024:.2f} KB"
    else:
        return f"{size} B"

def create_heatmap_figure(display_blocks, sample_sequences, access_matrix, seq_time_map, max_height=800):
    """创建热力图，但限制最大高度"""
    y_labels = []
    z_data = []

    def is_page_in_monitor_range(block, seq_idx):
        seq = sample_sequences[seq_idx]
        seq_time = seq_time_map.get(seq, 0)
        if seq_time < block.alloc_time:
            return False
        if block.is_freed and seq_time > block.free_time:
            return False
        return True

    for block in display_blocks:
        if not block:
            continue
        num_pages = (block.size + 4095) // 4096
        display_pages = min(num_pages, 16)

        for page_idx in range(display_pages):
            page_addr = block.addr + page_idx * 4096
            addr_str = f"0x{page_addr:012x}"
            y_labels.append(addr_str)

            row = []
            for seq_idx, seq in enumerate(sample_sequences):
                key = (block.addr, page_idx, seq)
                accessed = access_matrix.get(key, None)
                seq_time = seq_time_map.get(seq, 0)
                is_freed_period = block.is_freed and seq_time > block.free_time
                in_range = is_page_in_monitor_range(block, seq_idx)

                if is_freed_period:
                    row.append(3)
                elif accessed == 1:
                    row.append(2)
                elif accessed == 0:
                    row.append(1)
                elif in_range:
                    row.append(1)
                else:
                    row.append(0)

            z_data.append(row)

    if not z_data:
        return None

    x_labels = [f"第{i+1}轮" for i in range(len(sample_sequences))]

    fig = go.Figure(data=go.Heatmap(
        z=z_data,
        x=x_labels,
        y=y_labels,
        colorscale=[
            [0, '#ffffff'],
            [0.25, '#808080'],
            [0.5, '#00aa00'],
            [0.75, '#ff0000'],
            [1, '#ff0000']
        ],
        zmin=0,
        zmax=3,
        hoverongaps=False,
        showscale=True,
        colorbar=dict(
            title="状态",
            tickvals=[0.5, 1.5, 2.5, 3.5],
            ticktext=["无数据", "空闲", "已访问", "已释放"],
            len=0.5
        )
    ))

    # 动态高度但不超过 max_height
    calculated_height = max(300, len(y_labels) * 20)
    final_height = min(calculated_height, max_height)

    fig.update_layout(
        height=final_height,
        margin=dict(l=150, r=50, t=30, b=50),
        xaxis=dict(
            title="采样周期",
            tickmode='array',
            tickvals=list(range(len(sample_sequences)))[::max(1, len(sample_sequences)//10)],
            ticktext=[x_labels[i] for i in range(0, len(sample_sequences), max(1, len(sample_sequences)//10))]
        ),
        yaxis=dict(
            title="内存页地址 (4KB)",
            type='category',
            tickmode='linear'
        ),
        title=dict(
            text="内存页访问热力图",
            font=dict(size=14)
        )
    )

    return fig

def main():
    st.markdown("<h1 style='color: #1f77b4; margin-bottom: 10px;'>Lazy Visit - 内存分析工具</h1>", unsafe_allow_html=True)

    # Initialize session state
    if 'analyzed' not in st.session_state:
        st.session_state.analyzed = False
        st.session_state.reg_parser = None
        st.session_state.visit_parser = None
        st.session_state.resolver = None
        st.session_state.block_visits = None
        st.session_state.min_size_kb = 0
        st.session_state.selected_block_addr = None

    # ========== 顶部紧凑菜单栏 ==========
    st.markdown('<div class="top-menu">', unsafe_allow_html=True)
    menu_cols = st.columns([2, 2, 2, 1])

    with menu_cols[0]:
        st.markdown("<small>SO文件</small>", unsafe_allow_html=True)
        so_file = st.file_uploader("SO", type=['so'], label_visibility="collapsed", key="so_uploader")

    with menu_cols[1]:
        st.markdown("<small>分配日志</small>", unsafe_allow_html=True)
        reg_file = st.file_uploader("REG", type=['log'], label_visibility="collapsed", key="reg_uploader")

    with menu_cols[2]:
        st.markdown("<small>访问日志</small>", unsafe_allow_html=True)
        visit_file = st.file_uploader("VISIT", type=['log'], label_visibility="collapsed", key="visit_uploader")

    with menu_cols[3]:
        st.markdown("<br>", unsafe_allow_html=True)
        analyze_btn = st.button("分析", type="primary", disabled=not (reg_file and visit_file), use_container_width=True)

    st.markdown('</div>', unsafe_allow_html=True)

    # 未分析时的提示
    if not analyze_btn and not st.session_state.analyzed:
        st.info('请上传日志文件并点击「分析」按钮')
        return

    # ========== 分析逻辑 ==========
    if analyze_btn:
        with st.spinner("分析中..."):
            if so_file:
                with open("/tmp/temp.so", "wb") as f:
                    f.write(so_file.getvalue())
                resolver = ElfSymbolResolver("/tmp/temp.so")
            else:
                resolver = None

            with open("/tmp/mem_reg.log", "wb") as f:
                f.write(reg_file.getvalue())
            reg_parser = MemRegParser("/tmp/mem_reg.log")

            with open("/tmp/mem_visit.log", "wb") as f:
                f.write(visit_file.getvalue())
            visit_parser = MemVisitParser("/tmp/mem_visit.log")

            # Associate visit data with blocks
            block_visits = defaultdict(list)
            for visit in visit_parser.visits:
                for block in reg_parser.blocks.values():
                    if block.addr <= visit.vaddr < block.end_addr:
                        block_visits[block.addr].append(visit)
                        break

            # 计算每个块的冷热指标
            page_size = 4096
            for block in reg_parser.blocks.values():
                visits = block_visits.get(block.addr, [])
                if not visits:
                    continue

                block.total_pages = (block.size + page_size - 1) // page_size
                accessed_page_addrs = set()
                accessed_sequences = set()

                for visit in visits:
                    if visit.accessed == 1:
                        page_addr = (visit.vaddr // page_size) * page_size
                        accessed_page_addrs.add(page_addr)
                        accessed_sequences.add(visit.sequence)

                        if block.first_access_time is None:
                            block.first_access_time = visit.timestamp
                        block.last_access_time = visit.timestamp

                block.accessed_pages = len(accessed_page_addrs)
                block.access_count = len(accessed_sequences)
                block.page_access_rate = block.accessed_pages / block.total_pages if block.total_pages > 0 else 0

            # 分类函数
            def classify_block(block: MemBlock, total_seq: int) -> str:
                if block.access_count == 0:
                    return "Never Accessed"

                cold_hot_score = block.calc_cold_hot_score(total_seq)

                if block.is_freed:
                    if cold_hot_score < 0.3:
                        return "Cold Freed"
                    else:
                        return "Hot Freed"
                else:
                    if cold_hot_score < 0.2:
                        return "Ice Cold"
                    elif cold_hot_score < 0.4:
                        return "Cold"
                    elif cold_hot_score < 0.6:
                        return "Warm"
                    elif cold_hot_score < 0.8:
                        return "Hot"
                    else:
                        return "Burning Hot"

            # 预计算数据
            total_runtime = max(v.timestamp for v in visit_parser.visits) - min(v.timestamp for v in visit_parser.visits) if visit_parser.visits else 0
            total_sequences = len(visit_parser.sequences)
            min_alloc_time = min(b.alloc_time for b in reg_parser.blocks.values()) if reg_parser.blocks else 0

            block_data = {}
            for block in reg_parser.blocks.values():
                block.category = classify_block(block, total_sequences)
                block_data[block.addr] = {
                    'category': block.category,
                    'alloc_time_relative': block.alloc_time - min_alloc_time,
                    'last_access_relative': block.last_access_time - min_alloc_time if block.last_access_time else None,
                }

            # 构建 DataFrame
            df_data = []
            cumulative = 0
            for block in sorted(reg_parser.blocks.values(), key=lambda x: x.alloc_time):
                cumulative += block.actual_size
                df_data.append({
                    'Address': f"0x{block.addr:x}",
                    'Size': format_size(block.size),
                    'Size_Raw': block.size,
                    'Cumulative Size': format_size(cumulative),
                    'Cumulative_Raw': cumulative,
                    'Access Cycles': block.access_count,
                    'Accessed Pages': f"{block.accessed_pages}/{block.total_pages}",
                    'Page Rate': f"{block.page_access_rate*100:.1f}%",
                    'Cold/Hot Score': f"{block.calc_cold_hot_score(total_sequences):.2f}",
                    'Alloc Time': format_time(block.alloc_time - min_alloc_time),
                    'Alloc Time_Raw': block.alloc_time - min_alloc_time,
                    'Last Access': format_time(block.last_access_time - min_alloc_time) if block.last_access_time else "Never",
                    'Last Access_Raw': block.last_access_time - min_alloc_time if block.last_access_time else None,
                    'Category': block.category,
                    'Status': 'Freed' if block.is_freed else 'Alive',
                    'Block_Addr': block.addr
                })

            # 预构建访问矩阵
            access_matrix = {}
            sample_sequences = visit_parser.sequences[:min(200, len(visit_parser.sequences))]

            for block_addr, visits in block_visits.items():
                block = reg_parser.blocks.get(block_addr)
                if not block:
                    continue
                num_pages = min((block.size + 4095) // 4096, 20)

                for visit in visits:
                    if visit.sequence not in sample_sequences:
                        continue
                    page_idx = (visit.vaddr - block.addr) // 4096
                    if page_idx < num_pages:
                        key = (block_addr, page_idx, visit.sequence)
                        access_matrix[key] = visit.accessed

            # 构建序列时间映射
            seq_time_map = {}
            for v in visit_parser.visits:
                if v.sequence not in seq_time_map:
                    seq_time_map[v.sequence] = v.timestamp

            # Save to session state
            st.session_state.reg_parser = reg_parser
            st.session_state.visit_parser = visit_parser
            st.session_state.resolver = resolver
            st.session_state.block_visits = block_visits
            st.session_state.total_runtime = total_runtime
            st.session_state.total_sequences = total_sequences
            st.session_state.total_blocks = len(reg_parser.blocks)
            st.session_state.total_memory = sum(b.actual_size for b in reg_parser.blocks.values())
            st.session_state.min_alloc_time = min_alloc_time
            st.session_state.df_data = df_data
            st.session_state.df = pd.DataFrame(df_data)
            st.session_state.access_matrix = access_matrix
            st.session_state.sample_sequences = sample_sequences
            st.session_state.seq_time_map = seq_time_map
            st.session_state.analyzed = True
            st.session_state.selected_block_addr = None
            st.rerun()

    if not st.session_state.analyzed:
        return

    # 从 session state 读取数据
    reg_parser = st.session_state.reg_parser
    visit_parser = st.session_state.visit_parser
    resolver = st.session_state.resolver
    block_visits = st.session_state.block_visits
    total_runtime = st.session_state.total_runtime
    total_sequences = st.session_state.total_sequences
    total_blocks = st.session_state.total_blocks
    total_memory = st.session_state.total_memory
    min_alloc_time = st.session_state.min_alloc_time
    df = st.session_state.df
    access_matrix = st.session_state.access_matrix
    sample_sequences = st.session_state.sample_sequences
    seq_time_map = st.session_state.seq_time_map

    # ========== 统计面板 ==========
    st.markdown("### 统计面板")
    col1, col2, col3, col4 = st.columns(4)

    with col1:
        st.metric("运行时长", format_time(total_runtime))
    with col2:
        st.metric("扫描轮次", f"{total_sequences:,}")
    with col3:
        st.metric("内存块数", f"{total_blocks:,}")
    with col4:
        st.metric("总内存", format_size(total_memory))

    # ========== 分类筛选 + 大小过滤 ==========
    st.markdown("### 筛选条件")
    filter_cols = st.columns([3, 2, 2])

    with filter_cols[0]:
        categories = ["全部", "从未访问", "冷内存已释放", "热内存已释放", "极冷", "冷", "温热", "热", "极热"]
        category_to_english = {
            "全部": None,
            "从未访问": "Never Accessed",
            "冷内存已释放": "Cold Freed",
            "热内存已释放": "Hot Freed",
            "极冷": "Ice Cold",
            "冷": "Cold",
            "温热": "Warm",
            "热": "Hot",
            "极热": "Burning Hot"
        }
        selected_category = st.selectbox("分类", categories, key="category_filter")

    with filter_cols[1]:
        min_size_kb = st.number_input("最小大小(KB)", min_value=0, value=0, step=4)
        min_size_bytes = min_size_kb * 1024

    with filter_cols[2]:
        # 刷新按钮（重新渲染）
        if st.button("刷新视图", use_container_width=True):
            st.rerun()

    # 应用筛选
    if selected_category != "全部":
        filtered_df = df[df['Category'] == category_to_english[selected_category]]
    else:
        filtered_df = df

    if min_size_bytes > 0:
        filtered_df = filtered_df[filtered_df['Size_Raw'] >= min_size_bytes]

    filtered_addrs = filtered_df['Block_Addr'].tolist() if not filtered_df.empty else []
    filtered_blocks = [reg_parser.blocks.get(addr) for addr in filtered_addrs if addr in reg_parser.blocks]

    # ========== 主布局：左侧热力图 + 右侧详情 ==========
    st.markdown("---")

    main_col1, main_col2 = st.columns([3, 1])

    with main_col1:
        # 热力图区域（固定高度，可滚动）
        st.markdown("#### 内存页访问热力图")
        st.caption("说明: 每行=4KB页，每列=采样周期。绿=已访问，灰=空闲，红=已释放，白=无数据")

        # 使用 container 控制高度
        heatmap_container = st.container(height=500, border=True)

        with heatmap_container:
            if filtered_blocks:
                display_blocks = filtered_blocks[:30] if len(filtered_blocks) > 30 else filtered_blocks

                fig = create_heatmap_figure(
                    display_blocks,
                    sample_sequences,
                    access_matrix,
                    seq_time_map,
                    max_height=1200  # 允许内部滚动
                )

                if fig:
                    # 使用 use_container_width=False 保持比例
                    st.plotly_chart(fig, use_container_width=False, config={'responsive': True})
                else:
                    st.info("无数据可显示")
            else:
                st.info("请选择筛选条件以显示热力图")

    with main_col2:
        # 右侧详情面板
        st.markdown("#### 内存块详情")

        if not filtered_df.empty:
            # 选择器
            block_options = [f"0x{b.addr:x} ({format_size(b.size)})" for b in filtered_blocks if b]
            selected_block_str = st.selectbox(
                "选择块",
                block_options,
                key="block_select"
            )

            if selected_block_str:
                addr = int(selected_block_str.split()[0], 16)
                block = reg_parser.blocks.get(addr)

                if block:
                    # 详情卡片
                    st.markdown('<div class="detail-panel">', unsafe_allow_html=True)

                    st.markdown(f"**地址**  `0x{block.addr:x}`")
                    st.markdown(f"**大小**  {format_size(block.size)}")
                    st.markdown(f"**类型**  {block.alloc_type}")

                    status_color = "🔴" if block.is_freed else "🟢"
                    st.markdown(f"**状态**  {status_color} {'已释放' if block.is_freed else '存活'}")

                    st.divider()

                    st.markdown(f"**访问周期**  {block.access_count}/{total_sequences}")
                    st.markdown(f"**访问页面**  {block.accessed_pages}/{block.total_pages}")
                    st.markdown(f"**页面访问率**  {block.page_access_rate*100:.1f}%")

                    score = block.calc_cold_hot_score(total_sequences)
                    score_emoji = "🔥" if score > 0.6 else "❄️" if score < 0.3 else "⚡"
                    st.markdown(f"**冷热分数**  {score_emoji} {score:.2f}")

                    st.divider()

                    st.markdown(f"**分配时间**  {format_time(block.alloc_time - min_alloc_time)}")
                    if block.last_access_time:
                        st.markdown(f"**最后访问**  {format_time(block.last_access_time - min_alloc_time)}")
                    if block.is_freed:
                        st.markdown(f"**生命周期**  {format_time(block.lifetime_us)}")

                    st.markdown('</div>', unsafe_allow_html=True)

                    # 调用栈（可折叠）
                    with st.expander("📋 调用堆栈", expanded=True):
                        if block.callstack:
                            for i, pc in enumerate(block.callstack[:5]):
                                symbol = ""
                                if resolver:
                                    symbol = resolver.resolve_address(pc)

                                if symbol and not symbol.startswith("0x"):
                                    st.markdown(f"`#{i}` `{symbol}`")
                                    st.caption(f"`0x{pc:016x}`")
                                else:
                                    st.markdown(f"`#{i}` `0x{pc:016x}`")
                        else:
                            st.markdown("无调用堆栈")
        else:
            st.info("无匹配数据")

    # ========== 底部：内存块列表 ==========
    st.markdown("---")
    st.markdown("### 内存块列表")

    if not filtered_df.empty:
        display_df = filtered_df[['Address', 'Size', 'Access Cycles',
                                   'Accessed Pages', 'Page Rate', 'Cold/Hot Score',
                                   'Alloc Time', 'Last Access', 'Category', 'Status']].copy()
        display_df.columns = ['地址', '大小', '访问周期', '已访问页面', '页面访问率', '冷热分数',
                               '创建时间', '最后访问', '分类', '状态']
        st.dataframe(display_df, height=300, use_container_width=True)
    else:
        st.info("无匹配的内存块")

if __name__ == "__main__":
    main()