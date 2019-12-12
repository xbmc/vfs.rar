import hudson.plugins.throttleconcurrents.ThrottleJobProperty;
import com.cwctravel.hudson.plugins.extended_choice_parameter.ExtendedChoiceParameterDefinition;

/**
 * Simple wrapper step for building a plugin
 */
def call(Map addonParams = [:])
{

	def PLATFORMS_VALID = [
		'android-armv7': 'android',
		'android-aarch64': 'android-arm64-v8a',
		'ios-armv7': 'ios',
		'ios-aarch64': 'ios',
		'osx-x86_64': 'osx64',
		'ubuntu-ppa': 'linux',
		'windows-i686': 'windows/win32',
		'windows-x86_64': 'windows/x64',
		'windows-i686-uwp': 'windows/win32-uwp',
		'windows-x86_64-uwp': 'windows/x64-uwp'
	]
	def PLATFORMS_DEPLOY = [
		'android-armv7',
		'android-aarch64',
		'osx-x86_64',
		'ubuntu-ppa',
		'windows-i686',
		'windows-x86_64',
		'windows-i686-uwp',
		'windows-x86_64-uwp'
	]
	def UBUNTU_DISTS = [
		'eoan',
		'disco',
		'bionic',
		'xenial'
	]
	def VERSIONS_VALID = [
		'Leia': 'leia',
		'Matrix': 'matrix',
	]
	def PPAS_VALID = [
		'nightly': 'ppa:team-xbmc/xbmc-nightly',
		'unstable': 'ppa:team-xbmc/unstable',
		'stable': 'ppa:team-xbmc/ppa',
		'wsnipex-test': 'ppa:wsnipex/xbmc-addons-test'
	]
	def PPA_VERSION_MAP = [
		'Leia': 'stable',
		'Matrix': 'nightly',
	]

	properties([
		buildDiscarder(logRotator(artifactDaysToKeepStr: '', artifactNumToKeepStr: '', daysToKeepStr: '', numToKeepStr: '5')),
		disableConcurrentBuilds(),
		disableResume(),
		durabilityHint('PERFORMANCE_OPTIMIZED'),
		pipelineTriggers(env.BRANCH_NAME == 'Matrix' ? [cron('@weekly')] : null),
		[$class: 'RebuildSettings', autoRebuild: false, rebuildDisabled: true],
		[$class: 'ThrottleJobProperty', categories: [], limitOneJobWithMatchingParams: false, maxConcurrentPerNode: 0, maxConcurrentTotal: 1, paramsToUseForLimit: '', throttleEnabled: true, throttleOption: 'category'],
		parameters([
			extendedChoice('deployPlatforms', PLATFORMS_DEPLOY.join(','), PLATFORMS_DEPLOY.join(','), 'Platforms to deploy, deploy param from Jenkinsfile is always respected'),
			extendedChoice('PPA', PPAS_VALID.keySet().join(',')+',auto', 'auto', 'PPA to use'),
			extendedChoice('dists', UBUNTU_DISTS.join(','), UBUNTU_DISTS.join(','), 'Ubuntu version to build for'),
			string(defaultValue: '1', description: 'debian package revision tag', name: 'TAGREV', trim: true),
			booleanParam(defaultValue: false, description: 'Force upload to PPA', name: 'force_ppa_upload')
		])
	])

	def deployPlatforms = params.deployPlatforms.tokenize(',')
	def platforms = addonParams.containsKey('platforms') && addonParams.platforms.metaClass.respondsTo('each') && addonParams.platforms.every{ p -> p in PLATFORMS_VALID } ? addonParams.platforms : PLATFORMS_VALID.keySet()
	def deploy = addonParams.containsKey('deploy') && addonParams.deploy.metaClass.respondsTo('each') ? addonParams.deploy.findAll{ d -> d in platforms && d in PLATFORMS_DEPLOY && d in deployPlatforms } : PLATFORMS_DEPLOY
	def version = addonParams.containsKey('version') && addonParams.version in VERSIONS_VALID ? addonParams.version : VERSIONS_VALID.keySet()[0]
	def addon = env.JOB_NAME.tokenize('/')[1]
	/**
	 * Definition in case if an addon source code contains several addons,
	 * then the start name of the various addons with Asterix is given with e.g.
	 * `buildPlugin(archive: 'USED_PREFIX.*', ...)`.
	 */
	def archiveName = addonParams.containsKey('archive') && addonParams.containsKey('archive') != null ? addonParams.archive : addon
	Map tasks = [failFast: false]

	env.Configuration = 'Release'

	for (int i = 0; i < platforms.size(); ++i)
	{
		String platform = platforms[i]

		def category = "binary-addons/${platform}-${version}"
		if (ThrottleJobProperty.fetchDescriptor().getCategories().every{ c -> c.getCategoryName() !=  category})
		{
			ThrottleJobProperty.fetchDescriptor().getCategories().add(new ThrottleJobProperty.ThrottleCategory(category, 1, 0, null));
			ThrottleJobProperty.fetchDescriptor().save()
		}

		if (platform == 'ubuntu-ppa') continue

		tasks[platform] = {
			throttle(["binary-addons/${platform}-${version}"])
			{
				node(platform)
				{
					ws("workspace/binary-addons/kodi-${platform}-${version}")
					{
						stage("prepare (${platform})")
						{
							pwd = pwd()
							kodiBranch = version == "Matrix" ? "master" : version
							checkout([
								changelog: false,
								scm: [
									$class: 'GitSCM',
									branches: [[name: "*/${kodiBranch}"]],
									doGenerateSubmoduleConfigurations: false,
									extensions: [[$class: 'CloneOption', timeout: 20, honorRefspec: true, noTags: true, reference: "${pwd}/../../kodi"]],
									userRemoteConfigs: [[refspec: "+refs/heads/${kodiBranch}:refs/remotes/origin/${kodiBranch}", url: 'https://github.com/xbmc/xbmc.git']]
								]
							])

							if (isUnix())
							{
								folder = PLATFORMS_VALID[platform]
								sh "WORKSPACE=`pwd` sh -xe ./tools/buildsteps/${folder}/prepare-depends"
								folder = PLATFORMS_VALID[platform]
								sh "WORKSPACE=`pwd`" + (platform == 'ios-aarch64' ? ' DARWIN_ARM_CPU=arm64' : '') + " sh -xe ./tools/buildsteps/${folder}/configure-depends"
								folder = PLATFORMS_VALID[platform]
								sh "WORKSPACE=`pwd` sh -xe ./tools/buildsteps/${folder}/make-native-depends"
								sh "git clean -xffd -- tools/depends/target/binary-addons"
							}
							else
							{
								folder = PLATFORMS_VALID[platform]
								bat "tools/buildsteps/${folder}/prepare-env.bat"
								folder = PLATFORMS_VALID[platform]
								bat "tools/buildsteps/${folder}/download-dependencies.bat"
								bat "git clean -xffd -- tools/depends/target/binary-addons"
							}

							dir("tools/depends/target/binary-addons/${addon}")
							{
								if (env.BRANCH_NAME)
								{
									def scmVars = checkout(scm)
									currentBuild.displayName = scmVars.GIT_BRANCH + '-' + scmVars.GIT_COMMIT.substring(0, 7)
								}
								else if ((env.BRANCH_NAME == null) && (repo))
								{
									git repo
								}
								else
								{
									error 'buildPlugin must be used as part of a Multibranch Pipeline *or* a `repo` argument must be provided'
								}
							}

							dir("tools/depends/target/binary-addons/addons/${addon}")
							{
								writeFile file: "${addon}.txt", text: "${addon} . ."
								writeFile file: 'platforms.txt', text: 'all'
							}
						}

						stage("build (${platform})")
						{
							dir("tools/depends/target/binary-addons")
							{
								if (isUnix())
									sh "make -j $BUILDTHREADS ADDONS='${addon}' ADDONS_DEFINITION_DIR=`pwd`/addons ADDON_SRC_PREFIX=`pwd` EXTRA_CMAKE_ARGS=\"-DPACKAGE_ZIP=ON -DPACKAGE_DIR=`pwd`/../../../../cmake/addons/build/zips\" PACKAGE=1"
							}

							if (!isUnix())
							{
								env.ADDONS_DEFINITION_DIR = pwd().replace('\\', '/') + '/tools/depends/target/binary-addons/addons'
								env.ADDON_SRC_PREFIX = pwd().replace('\\', '/') + '/tools/depends/target/binary-addons'
								folder = PLATFORMS_VALID[platform]
								bat "tools/buildsteps/${folder}/make-addons.bat package ${addon}"
							}

							if (isUnix())
								sh "grep '${addon}' cmake/addons/.success"
						}

						stage("archive (${platform})")
						{
							archiveArtifacts artifacts: "cmake/addons/build/zips/${archiveName}+${platform}/${archiveName}-*.zip"
						}

						stage("deploy (${platform})")
						{
							if (platform in deploy && env.TAG_NAME != null)
							{
								echo "Deploying: ${addon} ${env.TAG_NAME}"
								versionFolder = VERSIONS_VALID[version]
								sshPublisher(
									publishers: [
										sshPublisherDesc(
											configName: 'Mirrors',
											transfers: [
												sshTransfer(
													execCommand: """\
RET_VALUE=0
mkdir -p /home/git/addons-binary/${versionFolder}
for addonDir in \$(ls -d upload/${archiveName}+${platform})
do
	addonFolder=\$(echo \${addonDir} | awk '{split(\$0,a,"/"); print a[2]}')
	chmod 444 \${addonDir}/${archiveName}.zip
	(mv \${addonDir}/ /home/git/addons-binary/${versionFolder}/ || \
	 cp \${addonDir}/${archiveName}-*.zip /home/git/addons-binary/${versionFolder}/\${addonFolder}/) 2> /dev/null
	PUBLISHED=\$?
	if [ \$PUBLISHED -ne 0 ]; then
		echo `ls upload/\${addonFolder}/${archiveName}-*.zip | cut -d / -f 2-` was already published >&2
		RET_VALUE=\$PUBLISHED
	fi
	rm -fr \${addonDir}/ 2> /dev/null
done
exit \$RET_VALUE
""",
													remoteDirectory: 'upload',
													removePrefix: 'cmake/addons/build/zips/',
													sourceFiles: "cmake/addons/build/zips/${archiveName}+${platform}/${archiveName}-*.zip"
												)
											]
										)
									]
								)
							}
						}
					}
				}
			}
		}
	}

	if ("ubuntu-ppa" in platforms && "ubuntu-ppa" in deploy)
	{
		platform = "ubuntu-ppa"
		tasks[platform] = {
			throttle(["binary-addons/${platform}-${version}"])
			{
				node(PLATFORMS_VALID[platform])
				{
					ws("workspace/binary-addons/kodi-${platform}-${version}")
					{
						def packageversion
						def dists = params.dists.tokenize(',')
						def ppas = params.PPA == "auto" ? [PPAS_VALID[PPA_VERSION_MAP[version]]] : []
						if (ppas.size() == 0)
						{
							params.PPA.tokenize(',').each{p -> ppas.add(PPAS_VALID[p])}
						}

						stage("clone ${platform}")
						{
							dir("${addon}")
							{
								if (env.BRANCH_NAME)
								{
									def scmVars = checkout(scm)
									currentBuild.displayName = scmVars.GIT_BRANCH + '-' + scmVars.GIT_COMMIT.substring(0, 7)
								}
								else if ((env.BRANCH_NAME == null) && (repo))
								{
									git repo
								}
								else
								{
									error 'buildPlugin must be used as part of a Multibranch Pipeline *or* a `repo` argument must be provided'
								}
							}
						}

						stage("build ${platform}")
						{
							if (params.force_ppa_upload)
							{
								sh "rm -f kodi-*.changes kodi-*.build kodi-*.upload"
							}

							dir("${addon}")
							{
								echo "Ubuntu dists enabled: ${dists} - TAGREV: ${params.TAGREV} - PPA: ${params.PPA}"
								def addonsxml = readFile "${addon}/addon.xml.in"
								packageversion = getVersion(addonsxml)
								echo "Detected PackageVersion: ${packageversion}"
								def changelogin = readFile 'debian/changelog.in'
								def origtarball = 'kodi-' + addon.replace('.', '-') + "_${packageversion}.orig.tar.gz"

								sh "git archive --format=tar.gz -o ../${origtarball} HEAD"

								for (dist in dists)
								{
									echo "Building debian-source package for ${dist}"
									def changelog = changelogin.replace('#PACKAGEVERSION#', packageversion).replace('#TAGREV#', params.TAGREV).replace('#DIST#', dist)
									writeFile file: "debian/changelog", text: "${changelog}"
									sh "debuild -S -k'jenkins (jenkins build bot) <jenkins@kodi.tv>'"
								}
							}
						}

						stage("deploy ${platform}")
						{
							if (env.TAG_NAME != null || params.force_ppa_upload)
							{
								def force = params.force_ppa_upload ? '-f' : ''
								def changespattern = 'kodi-' + addon.replace('.', '-') + "_${packageversion}-${params.TAGREV}*_source.changes"
								for (ppa in ppas)
								{
									echo "Uploading ${changespattern} to ${ppa}"
									sh "dput ${force} ${ppa} ${changespattern}"
								}
							}
						}
					}
				}
			}
		}
	}
	parallel(tasks)
}

def extendedChoice(name, choices, defaultchoice, desc)
{
	return new ExtendedChoiceParameterDefinition(
	        name /* String name */,
	        ExtendedChoiceParameterDefinition.PARAMETER_TYPE_MULTI_SELECT /* String type */,
	        choices /* String value */,
	        null /* String projectName */,
	        null /* String propertyFile */,
	        null /* String groovyScript */,
	        null /* String groovyScriptFile */,
	        null /* String bindings */,
	        null /* String groovyClasspath */,
	        null /* String propertyKey */,
	        defaultchoice /* String defaultValue */,
	        null /* String defaultPropertyFile */,
	        null /* String defaultGroovyScript */,
	        null /* String defaultGroovyScriptFile */,
	        null /* String defaultBindings */,
	        null /* String defaultGroovyClasspath */,
	        null /* String defaultPropertyKey */,
	        null /* String descriptionPropertyValue */,
	        null /* String descriptionPropertyFile */,
	        null /* String descriptionGroovyScript */,
	        null /* String descriptionGroovyScriptFile */,
	        null /* String descriptionBindings */,
	        null /* String descriptionGroovyClasspath */,
	        null /* String descriptionPropertyKey */,
	        null /* String javascriptFile */,
	        null /* String javascript */,
	        false /* boolean saveJSONParameterToFile*/,
	        false /* boolean quoteValue */,
	        choices.tokenize(',').size(), /* int visibleItemCount */,
	        desc /* String description */,
	        null /* String multiSelectDelimiter */
	)
}

@NonCPS
def getVersion(text) {
	def matcher = text =~ /version=\"([\d.]+)\"/
        matcher ? matcher.getAt(1)[1] : null
}
