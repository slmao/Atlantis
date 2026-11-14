# Complete the private managed producer only after the cooked outputs exist.
file(SHA256 "${png}" input_digest)
file(SHA256 "${artifact}" artifact_digest)
file(SHA256 "${metadata}" metadata_digest)
file(CONFIGURE OUTPUT "${evidence}" CONTENT "${input_digest}\n${artifact_digest}\n${metadata_digest}\n"
  @ONLY NEWLINE_STYLE UNIX)
file(TOUCH "${stamp}")
