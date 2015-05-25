
T ?= /cygdrive/g

DKit = $(T)/i51/i51KIT

toggle: Tcard
	@echo "T-card: $(T) $(DKit)"
	@ls "$(DKit)/i51TEST" 2>/dev/null ; true
	@ls "$(T)/i51/i51.log" 2>/dev/null ; true
	@echo "make <log|test|clean>"

log: Tcard
	cp -t. $(T)/i51/i51.log && echo -n > $(T)/i51/i51.log

test: Tcard
	touch $(T)/i51/i51.log
	touch $(T)/i51/i51KIT/i51TEST
	l=$(DKit)/i51TEST ; [ -e $l ] && echo "$l YES" || echo "$l NO"

Tcard: $(T)
	@[ -d $(DKit) ] || mkdir -p $(DKit)
	@case "$(toggle)" in log) rm $(T)/i51/i51.log 2>null || touch $(T)/i51/i51.log ;; server) rm $(DKit)/i51TEST 2>null || touch $(DKit)/i51TEST ;; esac

# find $$d -name "*.kit" -o -name "*.i51" -o -name "*.dll" -exec cp -vft "$(DKit)/$$d" '{}' \; ; 
Dirs=../i51SYS PKiGAPI 51pkpay 51pkGame paydo app-boot
sync: Tcard
	for d in $(Dirs); do \
		[ -d "$$d" ] || continue ; \
		[ -d "$(DKit)/$$d" ] || mkdir -p "$(DKit)/$$d" ; \
		find $$d | grep -E "(\.kit|\.i51|\.re|\.dll)$$" |xargs cp -vt "$(DKit)/$$d" ; \
	done
	cp -vRt $(DKit) Fonts

clean: Tcard
	for x in g h i; do [ -e /cygdrive/$$x/i51sn ] && rm -rf /cygdrive/$$x/i51sn ; done ; true
	rm -rf $(T)/i51/i51KIT/51pkpay ; true
	rm -rf $(T)/i51/i51SYS ; true
	rm -vf $(T)/i51/i51SYS/*.anti ; true
	find $(T)/i51

#-- #!/bin/sh
#-- 
#-- t=$T
#-- if [ -n "$1" ]; then t=$1; fi
#-- [ -z "$t" ] && exit 1
#-- 
#-- echo $t ; [ -d "$t" ] || exit 2
#-- 
#-- ls -v $t/i51/i51SYS 2>/dev/null
#-- rm -vf $t/i51/i51SYS/*.anti
#-- cp -vR ../../i51SYS $t/i51
#-- cp -vR ../PKiGAPI $t/i51/i51KIT
#-- 
#-- d=$t/i51/i51KIT/51pkpay ; [ -d $d ] || mkdir $d || exit 9
#-- for s in 51pkpay kitpay; do
#--     cp -vR ../$s/*.{i51,kit,re} $d 2>/dev/null
#--     cp -vR ../$s/Debug/*.dll $d 2>/dev/null
#-- done
#-- 
#-- s=paydo
#-- d=$t/i51/i51KIT/paydo ; [ -d $d ] || mkdir $d || exit 9
#-- cp -vR ../$s/*.{i51,kit,re} $d 2>/dev/null
#-- cp -vR ../$s/Debug/*.dll $d 2>/dev/null
#-- 
#-- cp -vR ../Fonts $d/..
#-- 
#-- d=$t/i51/i51KIT/app-boot
#-- [ -d "$d" ] || mkdir $d
#-- cp -vR ../app-boot/*.i51 $d
#-- 
#-- l=$t/i51/i51.log ; cp -vt. $l; date > $l
#-- 
#-- touch "$t/i51/`date +%F`"
#-- 
#-- l=$t/i51/i51KIT/i51TEST ; [ -e $l ] && echo "$l YES" || echo "$l NO"

