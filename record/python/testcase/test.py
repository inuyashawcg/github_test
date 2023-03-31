#！ /bin/python3

# 字符串替换
source_path = '/a/b/c/d'
source_prefix = '/a/b'
new_prefix = '/t'
new_path = source_path.replace(source_prefix, new_prefix)

print("new path is", new_path)