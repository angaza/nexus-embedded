# Do *not* be strict in execution, or some artifacts don't upload.
# For example, cppcheck won't upload the failed tests if we exit on any
# intermediate step having an exit code of 1.

# common artifacts directory expected to be present on the Docker container
ARTIFACTS_DIR=/buildkite_artifacts


function za-init {
    # used to pick up automatic conda environment activation
    # see https://github.com/conda/conda/issues/7980
    # `source` and `conda` are fairly verbose
    set +u
    source /opt/conda/etc/profile.d/conda.sh 2>/dev/null
    conda activate nexusemb 2>/dev/null
    set -u
    # 'rake aborted!
    # ArgumentError: invalid byte sequence in US-ASCII'
    # (Currently set directly in Dockerfile)
    #export LANG=C.UTF-8
    #export LC_ALL=C.UTF-8
}

function za-install-sonar-scanner {

    echo "--- Setting up SonarCloud binaries"

    # Get build wrapper, add to path
    # https://sonarcloud.io/documentation/analysis/languages/cfamily/
    # Note: "If you use macOS or Linux operating systems make sure your source tree is in a directory called src"
    SCANNER_BWRAPPER="build-wrapper-linux-x86"
    BWRAPPER_BIN="build-wrapper-linux-x86-64"
    wget -nc "https://angaza-misc.s3.amazonaws.com/sonarcloud/$SCANNER_BWRAPPER.zip"
    unzip -n "$SCANNER_BWRAPPER.zip"
    export PATH="$PWD/$SCANNER_BWRAPPER:$PATH"

    # https://community.sonarsource.com/t/i-didnt-get-right-result-in-ubuntu-18-04/7860
    cp "$SCANNER_BWRAPPER/libinterceptor-x86_64.so" "$SCANNER_BWRAPPER/libinterceptor-haswell.so"

    # Get SonarCloud scanner binary, add to path
    # https://sonarcloud.io/documentation/analysis/scan/sonarscanner/
    SCANNER_VERSION="4.1.0.1829-linux"

    wget -nc "https://angaza-misc.s3.amazonaws.com/sonarcloud/sonar-scanner-cli-$SCANNER_VERSION.zip"
    unzip -n "sonar-scanner-cli-$SCANNER_VERSION.zip"
    export PATH="$PWD/sonar-scanner-$SCANNER_VERSION/bin:$PATH"

}
