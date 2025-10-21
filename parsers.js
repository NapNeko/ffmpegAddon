const fs = require("fs");
const path = require("path");

// ---------- 配置模块 ----------
/**
 * 每个条目的配置结构:
 * {
 *   listFile: list 文件路径 (包含已启用的组件引用)
 *   targetFile: 目标文件路径 (包含 extern 声明)
 *   listPattern: 用于从 list 文件中提取已启用符号的正则
 *   externPattern: 用于匹配目标文件中 extern 声明的正则
 *   symbolType: 符号类型描述 (用于日志)
 * }
 */
const modules = [
    // libavcodec - parsers
    {
        listFile: "ffmpeg_src/libavcodec/parser_list.c",
        targetFile: "ffmpeg_src/libavcodec/parsers.c",
        listPattern: /&ff_([a-z0-9_]+)_parser\b/g,
        externPattern: /extern\s+const\s+AVCodecParser\s+ff_([a-z0-9_]+)_parser\s*;/,
        symbolType: "parser"
    },

    // libavcodec - codecs (encoders & decoders)
    {
        listFile: "ffmpeg_src/libavcodec/codec_list.c",
        targetFile: "ffmpeg_src/libavcodec/allcodecs.c",
        listPattern: /&ff_([a-z0-9_]+)_(?:encoder|decoder)\b/g,
        externPattern: /extern\s+const\s+FFCodec\s+ff_([a-z0-9_]+)_(?:encoder|decoder)\s*;/,
        symbolType: "codec"
    },

    // libavcodec - bitstream filters
    {
        listFile: "ffmpeg_src/libavcodec/bsf_list.c",
        targetFile: "ffmpeg_src/libavcodec/bitstream_filters.c",
        listPattern: /&ff_([a-z0-9_]+)_bsf\b/g,
        externPattern: /extern\s+const\s+FFBitStreamFilter\s+ff_([a-z0-9_]+)_bsf\s*;/,
        symbolType: "bsf"
    },

    // libavformat - demuxers
    {
        listFile: "ffmpeg_src/libavformat/demuxer_list.c",
        targetFile: "ffmpeg_src/libavformat/allformats.c",
        listPattern: /&ff_([a-z0-9_]+)_demuxer\b/g,
        externPattern: /extern\s+const\s+FFInputFormat\s+ff_([a-z0-9_]+)_demuxer\s*;/,
        symbolType: "demuxer"
    },

    // libavformat - muxers
    {
        listFile: "ffmpeg_src/libavformat/muxer_list.c",
        targetFile: "ffmpeg_src/libavformat/allformats.c",
        listPattern: /&ff_([a-z0-9_]+)_muxer\b/g,
        externPattern: /extern\s+const\s+FFOutputFormat\s+ff_([a-z0-9_]+)_muxer\s*;/,
        symbolType: "muxer"
    },

    // libavformat - protocols
    {
        listFile: "ffmpeg_src/libavformat/protocol_list.c",
        targetFile: "ffmpeg_src/libavformat/protocols.c",
        listPattern: /&ff_([a-z0-9_]+)_protocol\b/g,
        externPattern: /extern\s+const\s+URLProtocol\s+ff_([a-z0-9_]+)_protocol\s*;/,
        symbolType: "protocol"
    },
];

/**
 * 从 list 文件中提取已启用的符号名称
 * @param {string} listFilePath - list 文件路径
 * @param {RegExp} pattern - 提取符号的正则表达式
 * @returns {Set<string>} 符号名称集合
 */
function extractEnabledSymbols(listFilePath, pattern) {
    if (!fs.existsSync(listFilePath)) {
        console.warn(`⚠️  文件不存在: ${listFilePath}`);
        return new Set();
    }

    const content = fs.readFileSync(listFilePath, "utf-8");
    const symbols = new Set();

    // 重置正则的 lastIndex (全局匹配)
    pattern.lastIndex = 0;
    let match;

    while ((match = pattern.exec(content)) !== null) {
        if (match[1]) {
            symbols.add(match[1]);
        }
    }

    return symbols;
}

/**
 * 清理目标文件中未使用的 extern 声明
 * @param {Object} config - 模块配置对象
 */
function cleanExternDeclarations(config) {
    const { listFile, targetFile, listPattern, externPattern, symbolType } = config;

    console.log(`\n========== 处理 ${symbolType} ==========`);
    console.log(`List 文件: ${path.basename(listFile)}`);
    console.log(`目标文件: ${path.basename(targetFile)}`);

    // 检查文件是否存在
    if (!fs.existsSync(targetFile)) {
        console.error(`❌ 目标文件不存在: ${targetFile}`);
        return;
    }

    // 创建备份
    const bakFile = targetFile + ".bak";
    if (!fs.existsSync(bakFile)) {
        fs.copyFileSync(targetFile, bakFile);
        console.log(`📦 创建备份: ${path.basename(bakFile)}`);
    }

    // 提取已启用的符号
    const enabledSymbols = extractEnabledSymbols(listFile, listPattern);
    console.log(`✓ 已启用 ${symbolType} 数量: ${enabledSymbols.size}`);

    if (enabledSymbols.size === 0) {
        console.warn(`⚠️  未找到已启用的 ${symbolType},跳过清理`);
        return;
    }

    // 读取目标文件并过滤
    const content = fs.readFileSync(bakFile, "utf-8");
    const lines = content.split(/\r?\n/);
    const newLines = [];
    let removedCount = 0;

    for (const line of lines) {
        const match = line.match(externPattern);

        if (match && match[1]) {
            const symbolName = match[1];

            // 检查是否在已启用列表中
            if (!enabledSymbols.has(symbolName)) {
                console.log(`  ➖ 移除未使用: ff_${symbolName}_${symbolType}`);
                removedCount++;
                continue; // 跳过这一行
            }
        }

        newLines.push(line);
    }

    // 写回文件
    fs.writeFileSync(targetFile, newLines.join("\n"), "utf-8");
    console.log(`✅ ${path.basename(targetFile)} 已更新 (移除 ${removedCount} 个未使用的声明)`);
}

// ---------- 主执行流程 ----------
console.log("🚀 开始清理 FFmpeg extern 声明...\n");

let totalProcessed = 0;
let totalErrors = 0;

modules.forEach(config => {
    try {
        cleanExternDeclarations(config);
        totalProcessed++;
    } catch (error) {
        console.error(`❌ 处理 ${config.symbolType} 时出错:`, error.message);
        totalErrors++;
    }
});

console.log("\n" + "=".repeat(50));
console.log(`🎉 清理完成! 处理 ${totalProcessed} 个模块, ${totalErrors} 个错误`);
console.log("=".repeat(50));
