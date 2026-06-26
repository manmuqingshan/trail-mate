#!/bin/bash
# Fix project version from 0.0.0/0.1.0 to 0.1.30-alpha across all docs

cd /c/Users/vicliu/Projects/trail-mate

echo "=== Fixing 项目版本：0.0.0 -> 0.1.30-alpha in .md files ==="
find docs -name "*.md" -exec grep -l "0\.0\.0" {} \; | while read f; do
  sed -i 's/0\.0\.0/0.1.30-alpha/g' "$f"
  echo "  Fixed: $f"
done

echo ""
echo "=== Fixing 项目版本：0.1.0 -> 0.1.30-alpha in .md files ==="
find docs -name "*.md" -exec grep -l "项目版本：0\.1\.0" {} \; | while read f; do
  sed -i 's/项目版本：0\.1\.0/项目版本：0.1.30-alpha/g' "$f"
  echo "  Fixed: $f"
done

echo ""
echo "=== Fixing ### 0.1.0 - 2026 in .md files ==="
find docs -name "*.md" -exec grep -l "### 0\.1\.0 - 2026" {} \; | while read f; do
  sed -i 's/### 0\.1\.0 - 2026/### 0.1.30-alpha - 2026/g' "$f"
  echo "  Fixed: $f"
done

echo ""
echo "=== Fixing in .html files ==="
find docs -name "*.html" -exec grep -l "0\.1\.0\|0\.0\.0" {} \; | while read f; do
  sed -i 's/0\.1\.0/0.1.30-alpha/g; s/0\.0\.0/0.1.30-alpha/g' "$f"
  echo "  Fixed: $f"
done

echo ""
echo "=== Done ==="
