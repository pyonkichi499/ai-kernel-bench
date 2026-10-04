# claude-sandbox（コンテナ内で Claude Code を動かすコマンド）で使うイメージ。
# 開発イメージ（../Dockerfile、CI と同じもの）の上に、Claude Code を入れるだけ。
#
# ビルドは scripts/build-sandbox-image で行う（開発イメージ tangmen-dev:<ハッシュ> を作ってから、その上に重ねる）。
#
# claude-sandbox の要件:
#   - claude が PATH にあり、どの UID からも実行できる（/opt/claude に入れて全員に読み取りと実行を許す）
#   - git、curl、ca-certificates がある
#   - ENTRYPOINT が引数をそのまま実行する（土台の ubuntu には ENTRYPOINT がないので、そのまま実行される）
#
# TANGMEN_IN_CONTAINER=1 は土台で設定済み。コンテナ内の scripts/dev は docker を呼ばずにそのまま実行する。
# gh と gnupg は入れない。コンテナ内では push も署名もせず、コミットと push はホストで行う。
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive

# 土台にない curl を足す（git と ca-certificates は土台にある）
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl \
 && rm -rf /var/lib/apt/lists/*

RUN HOME=/opt/claude bash -c 'curl -fsSL https://claude.ai/install.sh | bash' \
 && ln -s "$(readlink -f /opt/claude/.local/bin/claude)" /usr/local/bin/claude \
 && chmod -R a+rX /opt/claude
ENV DISABLE_AUTOUPDATER=1
