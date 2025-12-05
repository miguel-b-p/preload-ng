#!/bin/bash
# benchmark_preload.sh

echo "=== BENCHMARK SEM PRELOAD ==="
# Para o preload e remove todos os resquícios
sudo systemctl stop preload

# Sincroniza todos os buffers pendentes para disco
sync

# Limpa completamente o page cache, dentries e inodes
# 1 = pagecache, 2 = dentries/inodes, 3 = ambos
echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null

# Libera swap se houver (força páginas em swap voltarem e serem descartadas)
sudo swapoff -a 2>/dev/null && sudo swapon -a 2>/dev/null

# Aguarda sistema estabilizar
sleep 5

echo "Cache limpo. Iniciando benchmark baseline..."
hyperfine --warmup 1 --runs 10 \
    --prepare 'sync; echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null; sleep 2' \
    'libreoffice --calc & xdotool search --sync --onlyvisible --class "libreoffice-calc" windowkill'

echo ""
echo "=== BENCHMARK COM PRELOAD ==="
sudo systemctl start preload

# Aguarda preload iniciar e fazer primeiras predições
echo "Aguardando preload aprender padrões (40s = 2 ciclos)..."
sleep 40

hyperfine --warmup 1 --runs 10 \
    --prepare 'sync; echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null; sleep 2' \
    'libreoffice --calc & xdotool search --sync --onlyvisible --class "libreoffice-calc" windowkill'
