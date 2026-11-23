"""Export the Urumqi prototype purchase list as a styled Excel workbook."""

from __future__ import annotations

import argparse
from collections.abc import Iterable
from pathlib import Path

from openpyxl import Workbook, load_workbook
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from openpyxl.utils import get_column_letter
from openpyxl.worksheet.datavalidation import DataValidation

Headers = (
    "序号",
    "分类",
    "材料名称",
    "规格与选型要求",
    "数量",
    "单位",
    "单价下限",
    "单价上限",
    "小计下限",
    "小计上限",
    "优先级",
    "采购备注",
)

CORE_ITEMS = [
    ("主控", "ESP32-WROOM-32 开发板", "Type-C、CH340、30/38Pin", 1, "块", 20, 35, "必买", "主控与USB串口"),
    ("线材", "Type-C 数据传输线", "支持数据传输，非充电线", 1, "根", 5, 12, "必买", "建议1m"),
    ("传感器", "TSL2561 光照模块", "3.3V I²C，地址0x39", 4, "个", 8, 14, "必买", "四向相对光照"),
    ("传感器", "TCA9548A I²C多路复用器", "8通道，3.3V兼容", 1, "个", 5, 12, "必买", "解决四路同地址"),
    ("传感器", "AS5600 磁编码器套件", "含径向磁铁，I²C", 1, "套", 12, 24, "推荐", "角度反馈与丢步诊断"),
    ("执行器", "TMC2209 步进驱动器", "STEP/DIR，附散热片", 1, "个", 15, 30, "必买", "24V供电"),
    ("执行器", "NEMA17 42BYGH40 或5:1减速电机", "优先自锁减速型", 1, "套", 35, 180, "必买", "2～3kg直驱余量不足"),
    ("机械", "联轴器", "5mm转减速器实际输出轴径", 1, "个", 8, 18, "必买", "下单前确认两端孔径"),
    ("电源", "24V 3A 开关电源", "DC输出，带过流保护", 1, "个", 35, 55, "必买", "不建议24V2A"),
    ("电源", "LM2596 降压模块", "24V转5V，建议带显示", 1, "个", 4, 8, "必买", "调压后锁定"),
    ("安全", "NC常闭机械限位开关", "带滚轮，常闭触点", 2, "个", 4, 8, "必买", "禁止使用常开触点"),
    ("安全", "NC自锁急停开关", "物理切断驱动使能", 1, "个", 8, 18, "必买", "不能仅用软件停止"),
    ("安全", "保险丝座与3A保险丝", "24V输入保护", 1, "套", 8, 15, "必买", "预留备用保险丝"),
    ("传感器", "DS18B20 防水温度传感器", "3.3V，1-Wire", 3, "个", 6, 10, "推荐", "膜面、背板、环境"),
    ("传感器", "BME280 温湿度气压模块", "3.3V I²C", 1, "个", 12, 22, "推荐", "环境温湿度和趋势"),
    ("原型", "400孔面包板", "标准半尺寸", 1, "块", 5, 10, "可选", "也可直接洞洞板焊接"),
    ("线材", "杜邦线套装", "公对母/母对母，约20cm", 1, "套", 8, 15, "必买", "多色便于布线"),
    ("线材", "24AWG耐低温硅胶线", "多色，柔软耐弯折", 1, "套", 15, 28, "推荐", "乌鲁木齐低温环境"),
]

MECH_ITEMS = [
    ("机械", "5mm内径轴承", "双轴承支承转轴", 2, "个", 6, 12, "必买", "建议带防尘盖"),
    ("机械", "304不锈钢M3/M4螺丝套装", "含螺母、垫片", 1, "套", 15, 30, "必买", "户外防锈"),
    ("机械", "2020/1515铝型材及角件", "外框4×320mm、内框4×247mm", 1, "套", 60, 130, "必买", "正方形对称接角下料"),
    ("机械", "激光切割亚克力窗扇", "3～5mm，建议247×247mm", 1, "片", 30, 60, "必买", "按实际装配槽复核"),
    ("材料", "辐射制冷膜样品", "先买小样验证附着与耐候", 1, "片", 20, 50, "必买", "不要直接购买大卷"),
    ("防护", "IP65防水接线盒", "建议200×120×75mm以上", 1, "个", 25, 50, "推荐", "容纳ESP32和驱动"),
    ("防护", "防水电缆接头", "M12/M16", 6, "个", 2, 4, "推荐", "电源、电机和传感器"),
    ("防护", "干燥剂与湿度指示卡", "可更换式", 1, "套", 5, 12, "推荐", "防结露"),
    ("耗材", "热缩管、扎带、绝缘胶带", "阻燃、耐低温", 1, "套", 15, 25, "必买", "线束绝缘与整理"),
    ("显示", "LCD1602 I2C显示屏", "3.3V或5V兼容", 1, "个", 8, 15, "可选", "答辩本地显示"),
    ("散热", "TMC2209铝制散热片", "20×20mm以上", 1, "个", 3, 8, "推荐", "驱动器散热"),
    ("防护", "5V小风扇和防尘网", "接线盒强制通风", 1, "套", 12, 25, "可选", "与密封方案配合"),
    ("光学", "透明PMMA光学窗片", "四向传感器保护窗", 4, "片", 3, 8, "推荐", "避免直接暴露"),
    ("安装", "AS5600磁铁定位压片/治具", "径向充磁磁铁同轴安装", 1, "套", 5, 15, "推荐", "保证磁铁与芯片间距"),
]

OUTDOOR_ITEMS = [
    ("气象", "三杯风速传感器", "脉冲输出，需电平保护", 1, "个", 60, 120, "增强", "当前固件需扩展"),
    ("气象", "翻斗式雨量计", "脉冲输出，适合户外", 1, "个", 50, 100, "增强", "当前固件需扩展"),
    ("低温", "24V PTC加热片与温控器", "控制盒防结露结冰", 1, "套", 25, 50, "增强", "建议独立温控"),
    ("防护", "不锈钢轴承保护盖", "防尘、防雪水", 2, "个", 8, 15, "增强", "与5mm转轴匹配"),
    ("防护", "耐低温波纹管", "线束防护", 1, "卷", 20, 40, "增强", "避免冬季线皮开裂"),
    ("防护", "防尘密封条/毛刷条", "降低风沙进入", 1, "套", 10, 25, "增强", "留出旋转间隙"),
    ("机械", "配重块与固定件", "将质心调回转轴", 1, "套", 20, 50, "增强", "优先于单纯增大电机"),
]

CHECK_ITEMS = (
    "确认正方形框架四边、板材厚度和接角方式",
    "测量完整质量与质心偏离转轴距离",
    "确认减速器输出轴径和联轴器孔径",
    "确认减速器自锁能力和额定输出力矩",
    "确认TSL2561工作电压与I²C地址",
    "确认四路传感器通过TCA9548A分通道",
    "确认AS5600含径向磁铁并完成零位标定",
    "确认限位和急停均为常闭触点",
    "确认24V电源为3A或以上",
    "设置TMC2209电流并安装散热片",
    "确认膜样品与结构胶/背胶兼容",
    "完成贴膜/不贴膜温度对照实验",
)

THIN = Side(style="thin", color="B7C4CE")
BORDER = Border(left=THIN, right=THIN, top=THIN, bottom=THIN)
TITLE_FILL = PatternFill("solid", fgColor="EAF0F5")
HEADER_FILL = PatternFill("solid", fgColor="244C68")
SUBHEADER_FILL = PatternFill("solid", fgColor="D9E5ED")
ALT_FILL = PatternFill("solid", fgColor="F5F8FA")


def set_cell(
    cell,
    *,
    font_name: str = "仿宋",
    size: float = 11,
    bold: bool = False,
    color: str = "1D2A36",
    fill: PatternFill | None = None,
    alignment: Alignment | None = None,
) -> None:
    cell.font = Font(name=font_name, size=size, bold=bold, color=color)
    if fill is not None:
        cell.fill = fill
    cell.alignment = alignment or Alignment(vertical="center")
    cell.border = BORDER


def add_item_sheet(
    workbook: Workbook,
    title: str,
    subtitle: str,
    items: Iterable[tuple],
) -> str:
    sheet = workbook.create_sheet(title)
    sheet.merge_cells("A1:L1")
    sheet.merge_cells("A2:L2")
    set_cell(
        sheet["A1"],
        font_name="宋体",
        size=18,
        bold=True,
        color="173B57",
        fill=TITLE_FILL,
        alignment=Alignment(horizontal="center", vertical="center"),
    )
    sheet["A1"] = title
    set_cell(
        sheet["A2"],
        size=10,
        color="5A6E7C",
        fill=TITLE_FILL,
        alignment=Alignment(horizontal="center", vertical="center"),
    )
    sheet["A2"] = subtitle
    sheet.row_dimensions[1].height = 32
    sheet.row_dimensions[2].height = 24

    for column, header in enumerate(Headers, start=1):
        cell = sheet.cell(row=4, column=column, value=header)
        set_cell(
            cell,
            font_name="黑体",
            size=10.5,
            bold=True,
            color="FFFFFF",
            fill=HEADER_FILL,
            alignment=Alignment(horizontal="center", vertical="center", wrap_text=True),
        )
    sheet.row_dimensions[4].height = 28

    rows = list(items)
    first_data_row = 5
    for offset, item in enumerate(rows):
        row = first_data_row + offset
        (
            category,
            name,
            specification,
            quantity,
            unit,
            price_low,
            price_high,
            priority,
            note,
        ) = item
        values = (
            offset + 1,
            category,
            name,
            specification,
            quantity,
            unit,
            price_low,
            price_high,
            f"=E{row}*G{row}",
            f"=E{row}*H{row}",
            priority,
            note,
        )
        for column, value in enumerate(values, start=1):
            cell = sheet.cell(row=row, column=column, value=value)
            set_cell(
                cell,
                size=10.5,
                bold=column == 3,
                fill=ALT_FILL if offset % 2 else None,
                alignment=Alignment(
                    horizontal="center" if column in (1, 5, 6, 7, 8, 9, 10, 11) else "left",
                    vertical="center",
                    wrap_text=True,
                ),
            )
            if column in (7, 8, 9, 10):
                cell.number_format = '¥0.00'
        sheet.row_dimensions[row].height = 38

    last_data_row = first_data_row + len(rows) - 1
    total_row = last_data_row + 1
    sheet.merge_cells(start_row=total_row, start_column=1, end_row=total_row, end_column=6)
    set_cell(
        sheet.cell(row=total_row, column=1, value="本表参考预算"),
        font_name="黑体",
        bold=True,
        fill=SUBHEADER_FILL,
        alignment=Alignment(horizontal="right", vertical="center"),
    )
    for column in (7, 8):
        set_cell(sheet.cell(row=total_row, column=column), fill=SUBHEADER_FILL)
    for column in (9, 10):
        cell = sheet.cell(
            row=total_row,
            column=column,
            value=f"=SUM({get_column_letter(column)}{first_data_row}:"
            f"{get_column_letter(column)}{last_data_row})",
        )
        set_cell(cell, font_name="黑体", bold=True, fill=SUBHEADER_FILL)
        cell.number_format = '¥0.00'
    for column in (11, 12):
        set_cell(sheet.cell(row=total_row, column=column), fill=SUBHEADER_FILL)

    widths = (6, 10, 27, 34, 8, 8, 12, 12, 13, 13, 9, 28)
    for index, width in enumerate(widths, start=1):
        sheet.column_dimensions[get_column_letter(index)].width = width
    sheet.freeze_panes = "A5"
    sheet.auto_filter.ref = f"A4:L{last_data_row}"
    sheet.sheet_view.showGridLines = False
    sheet.page_setup.orientation = "landscape"
    sheet.page_setup.fitToWidth = 1
    sheet.page_setup.fitToHeight = 0
    sheet.sheet_properties.pageSetUpPr.fitToPage = True
    sheet.print_title_rows = "1:4"
    sheet.oddFooter.center.text = "第 &P 页 / 共 &N 页"
    return f"'{title}'!I{first_data_row}:I{last_data_row}", f"'{title}'!J{first_data_row}:J{last_data_row}"


def add_summary(
    workbook: Workbook,
    ranges: dict[str, tuple[str, str]],
) -> None:
    sheet = workbook.create_sheet("封面与汇总", 0)
    sheet.sheet_view.showGridLines = False
    sheet.merge_cells("A1:H2")
    set_cell(
        sheet["A1"],
        font_name="宋体",
        size=22,
        bold=True,
        color="173B57",
        fill=TITLE_FILL,
        alignment=Alignment(horizontal="center", vertical="center"),
    )
    sheet["A1"] = "智能环境自适应旋转窗控制系统\n硬件采购与预算清单"
    sheet.row_dimensions[1].height = 36
    sheet.row_dimensions[2].height = 36

    sheet.merge_cells("A3:H3")
    set_cell(
        sheet["A3"],
        size=10.5,
        color="5A6E7C",
        fill=TITLE_FILL,
        alignment=Alignment(horizontal="center"),
    )
    sheet["A3"] = "适用地区：乌鲁木齐　|　正方形外框320 mm　|　质量2～3 kg　|　版本：v1.3.1"

    project_rows = [
        ("内框内边", 90.5, "mm", "正方形内开口边长"),
        ("内框外边", 247.0, "mm", "正方形内框外边长"),
        ("外框外边", 320.0, "mm", "正方形整体外框边长"),
        ("移动部件质量", 2.5, "kg", "设计区间2～3 kg"),
        ("默认质心偏心", 10.0, "mm", "必须通过实测确认"),
        ("推荐减速比", 5.0, ":1", "优先自锁蜗轮/行星减速"),
    ]
    sheet.append([])
    sheet.merge_cells("A5:D5")
    set_cell(sheet["A5"], font_name="黑体", size=14, bold=True, fill=SUBHEADER_FILL)
    sheet["A5"] = "一、设计输入"
    for offset, row_values in enumerate(project_rows, start=6):
        for column, value in enumerate(row_values, start=1):
            cell = sheet.cell(row=offset, column=column, value=value)
            set_cell(cell, alignment=Alignment(horizontal="center", vertical="center"))

    sheet.merge_cells("F5:H5")
    set_cell(sheet["F5"], font_name="黑体", size=14, bold=True, fill=SUBHEADER_FILL)
    sheet["F5"] = "二、预算汇总"
    summary_rows = [
        ("核心采购", ranges["核心采购"], "霍尔、主控、驱动、电源与安全件"),
        ("机械加工", ranges["机械加工"], "型材、窗扇、膜样与防护"),
        ("户外增强", ranges["户外增强"], "风、雨雪、加热和配重"),
    ]
    for offset, (label, (low_formula, high_formula), note) in enumerate(summary_rows, start=6):
        sheet.cell(row=offset, column=6, value=label)
        sheet.cell(row=offset, column=7, value=f"=SUM({low_formula})")
        sheet.cell(row=offset, column=8, value=f"=SUM({high_formula})")
        for column in range(6, 9):
            set_cell(
                sheet.cell(row=offset, column=column),
                alignment=Alignment(horizontal="center", vertical="center", wrap_text=True),
            )
        sheet.cell(row=offset, column=6).comment = None
        sheet.cell(row=offset, column=6).value = f"{label}\n{note}"
        if column in (7, 8):
            pass

    for row in range(7, 9):
        sheet.cell(row=row, column=7).number_format = '¥0.00'
        sheet.cell(row=row, column=8).number_format = '¥0.00'
    sheet.cell(row=6, column=7).number_format = '¥0.00'
    sheet.cell(row=6, column=8).number_format = '¥0.00'
    sheet.cell(row=9, column=6, value="合计")
    sheet.cell(row=9, column=7, value="=SUM(G6:G8)")
    sheet.cell(row=9, column=8, value="=SUM(H6:H8)")
    for column in range(6, 9):
        cell = sheet.cell(row=9, column=column)
        set_cell(
            cell,
            font_name="黑体",
            bold=True,
            fill=SUBHEADER_FILL,
            alignment=Alignment(horizontal="center", vertical="center"),
        )
        if column in (7, 8):
            cell.number_format = '¥0.00'

    sheet.merge_cells("A13:D13")
    set_cell(sheet["A13"], font_name="黑体", size=14, bold=True, fill=SUBHEADER_FILL)
    sheet["A13"] = "三、驱动校核结论"
    sheet.merge_cells("A14:D15")
    sheet["A14"] = (
        "2.5 kg、质心偏心10 mm时需求约0.306 N·m；42BYGH40直驱可用约"
        "0.34 N·m，余量仅1.11×。建议5:1自锁减速，并将质心调回转轴。"
    )
    sheet["A14"].alignment = Alignment(wrap_text=True, vertical="center")

    sheet.merge_cells("F13:H13")
    set_cell(sheet["F13"], font_name="黑体", size=14, bold=True, fill=SUBHEADER_FILL)
    sheet["F13"] = "四、预算建议"
    sheet.merge_cells("F14:H15")
    sheet["F14"] = "室内答辩精简版约520～680元；乌鲁木齐户外增强版约750～1000元。"
    sheet["F14"].alignment = Alignment(wrap_text=True, vertical="center")

    sheet.merge_cells("A17:H18")
    sheet["A17"] = (
        "说明：表中价格为参考零售价，未含运费。风速、雨雪和温度设备购买前需扩展固件采集"
        "与安全逻辑。窗框为正方形；实测质心与减速器输出轴径确定后，方可最终下料。"
    )
    sheet["A17"].alignment = Alignment(wrap_text=True, vertical="center")
    sheet["A17"].fill = PatternFill("solid", fgColor="FFF7E6")
    sheet["A17"].font = Font(name="仿宋", size=10.5, color="7A4E00")
    sheet["A17"].border = BORDER

    for column, width in enumerate((16, 13, 12, 24, 3, 28, 16, 16), start=1):
        sheet.column_dimensions[get_column_letter(column)].width = width
    sheet.page_setup.orientation = "landscape"
    sheet.page_setup.fitToWidth = 1
    sheet.sheet_properties.pageSetUpPr.fitToPage = True


def add_square_geometry_sheet(workbook: Workbook) -> None:
    sheet = workbook.create_sheet("正方形加工")
    sheet.merge_cells("A1:E1")
    set_cell(
        sheet["A1"],
        font_name="宋体",
        size=18,
        bold=True,
        color="173B57",
        fill=TITLE_FILL,
        alignment=Alignment(horizontal="center", vertical="center"),
    )
    sheet["A1"] = "正方形窗框加工尺寸"

    headers = ("项目", "计算式/名义值", "数值", "单位", "说明")
    for column, header in enumerate(headers, start=1):
        cell = sheet.cell(row=3, column=column, value=header)
        set_cell(
            cell,
            font_name="黑体",
            bold=True,
            color="FFFFFF",
            fill=HEADER_FILL,
            alignment=Alignment(horizontal="center", vertical="center"),
        )

    rows = [
        ("内框开口边长", "输入", 90.5, "mm", "正方形内开口"),
        ("内框外边边长", "输入", 247.0, "mm", "正方形内框外轮廓"),
        ("外框外边边长", "输入", 320.0, "mm", "正方形整体外轮廓"),
        ("内框单侧框宽", "=(247.0-90.5)/2", "=(C5-C4)/2", "mm", "内框外边至开口的单侧距离"),
        ("外框单侧框宽", "=(320.0-247.0)/2", "=(C6-C5)/2", "mm", "外框外边至内框外边的单侧距离"),
        ("开口对角线", "90.5×√2", "=C4*SQRT(2)", "mm", "开口最大对角线"),
        ("内框对角线", "247×√2", "=C5*SQRT(2)", "mm", "内框外边最大对角线"),
        ("外框对角线", "320×√2", "=C6*SQRT(2)", "mm", "外框最大对角线"),
        ("外框名义下料", "4段，名义边长320", 320.0, "mm", "最终按型材接角方式扣除接缝"),
        ("内框名义下料", "4段，名义边长247", 247.0, "mm", "最终按型材接角方式扣除接缝"),
        ("窗扇面板参考", "247×247", 247.0, "mm", "若面板装在开口内则按90.5×90.5"),
    ]
    for index, values in enumerate(rows, start=4):
        for column, value in enumerate(values, start=1):
            cell = sheet.cell(row=index, column=column, value=value)
            set_cell(
                cell,
                fill=ALT_FILL if index % 2 == 0 else None,
                alignment=Alignment(
                    horizontal="center" if column in (3, 4) else "left",
                    vertical="center",
                    wrap_text=True,
                ),
            )
            if isinstance(value, (int, float)) and column == 3:
                cell.number_format = "0.00"
        sheet.row_dimensions[index].height = 34

    sheet.merge_cells("A16:E18")
    sheet["A16"] = (
        "结构提示：正方形外框名义边长为320 mm，内外框必须四边对称。轴承、转轴和"
        "电机支架应围绕正方形中心布置，使2～3 kg窗扇质心尽量接近转轴中心。"
    )
    sheet["A16"].alignment = Alignment(wrap_text=True, vertical="center")
    sheet["A16"].fill = PatternFill("solid", fgColor="FFF7E6")
    sheet["A16"].font = Font(name="仿宋", size=10.5, color="7A4E00")
    sheet["A16"].border = BORDER

    for column, width in enumerate((24, 34, 14, 10, 48), start=1):
        sheet.column_dimensions[get_column_letter(column)].width = width
    sheet.freeze_panes = "A4"
    sheet.sheet_view.showGridLines = False
    sheet.page_setup.orientation = "landscape"
    sheet.page_setup.fitToWidth = 1
    sheet.sheet_properties.pageSetUpPr.fitToPage = True


def add_check_sheet(workbook: Workbook) -> None:
    sheet = workbook.create_sheet("采购核对")
    sheet.merge_cells("A1:D1")
    set_cell(
        sheet["A1"],
        font_name="宋体",
        size=18,
        bold=True,
        fill=TITLE_FILL,
        alignment=Alignment(horizontal="center", vertical="center"),
    )
    sheet["A1"] = "采购前核对记录"
    headers = ("序号", "核对事项", "状态", "备注")
    for column, header in enumerate(headers, start=1):
        cell = sheet.cell(row=3, column=column, value=header)
        set_cell(
            cell,
            font_name="黑体",
            bold=True,
            color="FFFFFF",
            fill=HEADER_FILL,
            alignment=Alignment(horizontal="center"),
        )
    status_validation = DataValidation(
        type="list",
        formula1='"未核对,已确认,不适用"',
        allow_blank=False,
    )
    sheet.add_data_validation(status_validation)
    for index, item in enumerate(CHECK_ITEMS, start=1):
        row = index + 3
        for column, value in enumerate((index, item, "未核对", ""), start=1):
            cell = sheet.cell(row=row, column=column, value=value)
            set_cell(
                cell,
                fill=ALT_FILL if index % 2 == 0 else None,
                alignment=Alignment(
                    horizontal="center" if column in (1, 3) else "left",
                    vertical="center",
                    wrap_text=True,
                ),
            )
        status_validation.add(sheet.cell(row=row, column=3))
        sheet.row_dimensions[row].height = 32
    sheet.column_dimensions["A"].width = 7
    sheet.column_dimensions["B"].width = 65
    sheet.column_dimensions["C"].width = 14
    sheet.column_dimensions["D"].width = 32
    sheet.freeze_panes = "A4"
    sheet.auto_filter.ref = f"A3:D{3 + len(CHECK_ITEMS)}"
    sheet.sheet_view.showGridLines = False
    sheet.page_setup.orientation = "landscape"
    sheet.page_setup.fitToWidth = 1
    sheet.sheet_properties.pageSetUpPr.fitToPage = True


def build_workbook(output: Path) -> None:
    workbook = Workbook()
    workbook.remove(workbook.active)
    ranges: dict[str, tuple[str, str]] = {}
    ranges["核心采购"] = add_item_sheet(
        workbook,
        "核心采购",
        "主控、传感器、执行器、电源和安全件",
        CORE_ITEMS,
    )
    ranges["机械加工"] = add_item_sheet(
        workbook,
        "机械加工",
        "2～3 kg窗扇、320 mm外框、轴承、膜样和防护",
        MECH_ITEMS,
    )
    ranges["户外增强"] = add_item_sheet(
        workbook,
        "户外增强",
        "乌鲁木齐风沙、低温、雨雪和结露防护",
        OUTDOOR_ITEMS,
    )
    add_summary(workbook, ranges)
    add_square_geometry_sheet(workbook)
    add_check_sheet(workbook)
    workbook.properties.title = "智能环境自适应旋转窗控制系统硬件采购清单"
    workbook.properties.subject = "乌鲁木齐条件，320 mm外框，2～3 kg原型"
    workbook.properties.creator = "LJYI2120"
    workbook.properties.keywords = "智能窗, 辐射制冷, 采购清单, 乌鲁木齐"
    workbook.active = 0
    workbook.save(output)

    reopened = load_workbook(output, read_only=True, data_only=False)
    expected = {
        "封面与汇总",
        "核心采购",
        "机械加工",
        "户外增强",
        "正方形加工",
        "采购核对",
    }
    if set(reopened.sheetnames) != expected:
        raise RuntimeError("generated workbook sheets do not match")
    reopened.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists() and not args.force:
        raise FileExistsError(f"refusing to overwrite existing file: {args.output}")
    temporary = args.output.with_name(f"{args.output.stem}.tmp{args.output.suffix}")
    build_workbook(temporary)
    temporary.replace(args.output)
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
