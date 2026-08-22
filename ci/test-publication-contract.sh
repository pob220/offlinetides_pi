#!/bin/sh
set -eu

source_dir=${1:-.}
config=$source_dir/.circleci/config.yml
deploy=$source_dir/ci/deploy-alpha-artifacts.sh

grep -q 'default: false' "$config"
grep -q 'only: alpha' "$config"
grep -q 'hold-for-alpha-approval' "$config"
grep -q 'context: offlinetides-deployment' "$config"
grep -q 'CIRCLE_BRANCH:-' "$deploy"
grep -q 'OFFLINETIDES_PUBLICATION_APPROVED' "$deploy"
grep -q 'CLOUDSMITH_API_KEY' "$deploy"

if grep -Eq '(CLOUDSMITH_API_KEY:|cloudsmith push)' "$config"; then
  echo "Validation configuration must not embed credentials or upload commands" >&2
  exit 1
fi
echo "OfflineTides publication contract passed"
