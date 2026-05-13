#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
分析并修复路径编码问题
"""
import os
import json
import sys

def analyze_encoding():
    """分析编码问题"""
    config_env_path = 'build/config.env'
    
    if not os.path.exists(config_env_path):
        print(f"Error: {config_env_path} not found")
        return
    
    # 读取 config.env
    with open(config_env_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    config = json.loads(content)
    
    # 检查路径
    print("=== 分析 config.env 中的路径 ===")
    for key in ['COMPONENT_KCONFIGS_SOURCE_FILE', 'COMPONENT_KCONFIGS_PROJBUILD_SOURCE_FILE']:
        if key in config:
            path = config[key]
            print(f"\n{key}:")
            print(f"  原始路径: {repr(path)}")
            print(f"  显示路径: {path}")
            
            # 检查路径是否存在
            if os.path.exists(path):
                print(f"  ✓ 路径存在")
            else:
                print(f"  ✗ 路径不存在")
                # 尝试修复编码
                try:
                    # 假设路径被错误编码为 GBK，然后被当作 UTF-8 读取
                    # 我们需要反向操作
                    fixed_path = path.encode('utf-8').decode('gbk', errors='ignore')
                    print(f"  尝试修复为: {fixed_path}")
                    if os.path.exists(fixed_path):
                        print(f"  ✓ 修复后的路径存在！")
                        config[key] = fixed_path
                except Exception as e:
                    print(f"  修复失败: {e}")

if __name__ == '__main__':
    analyze_encoding()

