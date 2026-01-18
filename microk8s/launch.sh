#!/bin/bash
HOST="$(hostname).mshome.net"
SKEY=$(openssl rand -base64 32)

# 2. Generate the Launch Configuration File
cat <<EOF > microk8s-config.yaml
---
version: 0.1.0
addons:
  - name: rbac
  - name: ingress
  - name: hostpath-storage
  - name: metrics-server

extraKubeAPIServerArgs:
  --encryption-provider-config: "\$SNAP_DATA/args/encryption-config.yaml"
  --authorization-mode: RBAC,Node
  --audit-policy-file: "\$SNAP_DATA/args/audit-policy.yaml"
  --audit-log-path: "\$SNAP_DATA/var/log/kube-apiserver-audit.log"
  --audit-log-maxage: "30"
  --audit-log-maxbackup: "10"
  --audit-log-maxsize: "100"

extraSANs:
  - $HOST

extraConfigFiles:
  encryption-config.yaml: |
    apiVersion: apiserver.config.k8s.io/v1
    kind: EncryptionConfiguration
    resources:
      - resources:
          - secrets
          - configmaps
        providers:
          - aescbc:
              keys:
                - name: key1
                  secret: $SKEY
          - identity: {}

  audit-policy.yaml: |
    apiVersion: audit.k8s.io/v1
    kind: Policy
    rules:
      # A. Secrets, ConfigMaps, TokenReviews: Metadata ONLY
      # CIS Requirement: Avoid logging sensitive data bodies.
      - level: Metadata
        resources:
        - group: ""
          resources: ["secrets", "configmaps"]
        - group: "authentication.k8s.io"
          resources: ["tokenreviews"]

      # B. Critical Workload Modifications: RequestResponse
      # CIS Requirement: Log changes to Pods, Deployments, and RBAC.
      - level: RequestResponse
        verbs: ["create", "update", "patch", "delete", "deletecollection"]
        resources:
        - group: ""
          resources: ["pods", "services", "serviceaccounts"]
        - group: "apps"
          resources: ["deployments", "replicasets", "daemonsets", "statefulsets"]
        - group: "rbac.authorization.k8s.io"
          resources: ["roles", "rolebindings", "clusterroles", "clusterrolebindings"]

      # C. Intrusive Actions (Exec/Attach): RequestResponse
      # CIS Requirement: Log use of pods/exec and proxies.
      - level: RequestResponse
        resources:
        - group: ""
          resources: ["pods/exec", "pods/attach", "pods/portforward", "services/proxy"]

      # D. Catch-all: Metadata
      # Log everything else at low detail for traceability.
      - level: Metadata
        omitStages:
          - "RequestReceived"
EOF
