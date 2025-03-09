while true; do
  clear
  date
  echo "=== NODES ==="
  kubectl get nodes | sort
  echo ""
  echo "=== JOB STATUS ==="
  kubectl get jobs | awk '{print $1, $2, $3}' | sort
  echo ""
  echo "=== POD STATUS ==="
  kubectl get pods --no-headers | awk '{print $3}' | sort | uniq -c
  echo ""
  sleep 30
done