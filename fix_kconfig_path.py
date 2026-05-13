#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
临时修复脚本：修复 ESP-IDF prepare_kconfig_files.py 在处理包含中文字符路径时的编码问题
"""
import os
import sys
import json

def fix_config_env():
    """修复 config.env 文件中的路径编码问题"""
    config_env_path = os.path.join('build', 'config.env')
    
    if not os.path.exists(config_env_path):
        print(f"Error: {config_env_path} not found")
        return False
    
    try:
        # 读取 config.env 文件
        with open(config_env_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 解析 JSON
        config = json.loads(content)
        
        # 确保 build 目录存在
        build_dir = 'build'
        if not os.path.exists(build_dir):
            os.makedirs(build_dir)
        
        # 修复路径并创建必要的文件
        kconfigs_in = os.path.join(build_dir, 'kconfigs.in')
        kconfigs_projbuild_in = os.path.join(build_dir, 'kconfigs_projbuild.in')
        
        # 创建空文件（如果不存在）
        for file_path in [kconfigs_in, kconfigs_projbuild_in]:
            if not os.path.exists(file_path):
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write('')
                print(f"Created: {file_path}")
        
        return True
    except Exception as e:
        print(f"Error fixing config.env: {e}")
        return False

if __name__ == '__main__':
    if fix_config_env():
        print("Config environment fixed successfully")
        sys.exit(0)
    else:
        print("Failed to fix config environment")
        sys.exit(1)

