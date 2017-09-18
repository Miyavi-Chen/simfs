
#the parameter has to be put outside the function to get the right result
num_dir=$1
dest_location=/dev/shm/
test_fs_mkdir()
{
count=$num_dir

echo "The expected direroty number:${num_dir}"

while [ $count -gt 0 ]
do
	mkdir $dest_location/$count
	((count = $count - 1))
	cd $dest_location
	ls -al
	echo ""
done	

}

test_fs_mkdir_nested()
{
count=$num_dir
echo "The expected directory level is:$count"

cd $dest_location
echo "We are in $PWD"

while [ $count -gt 0 ]
do
	mkdir ./$count
	cd $count
	echo "The current directory:$PWD"
	((count = $count -1 ))
done	
	
}

dir_length=$1
test_fs_mkdir_long_name()
{
total_length=$dir_length
echo "The dirname will increase from 1 to $dir_length"

temp_file=/dev/shm/temp.string
cd $dest_location
echo "We are in the $PWD"

#first clean out the file
>$temp_file
while [ $total_length -gt 0 ]
do
	#first generate the string containing the file name of targeted length
	echo -e "${total_length}\c" >> $temp_file
	mkdir `cat $temp_file` #now we get the countent back
	ls -al
	((total_length = $total_length - 1))
done
 
}

nested_level=$1
test_fs_mkdir_nested_long_name()
{
temp_file=/dev/shm/temp.string	
count=$nested_level
echo "The expected directory level is:$count"

dir_length

cd $dest_location
echo "We are in $PWD"

while [ $count -gt 0 ]
do
	#reset the dir_length for the target name
	((dir_length = $count + 10 ))
	while [ $dir_length -gt 0 ]
	do
		echo -e "${dir_length}\c" >> $temp_file
		((dir_length = $dir_length - 1))
	done

	#mkdir according to the file contents(the name)
	mkdir `cat $temp_file`
	cd `cat $temp_file`
	echo "The current directory:$PWD"
	((count = $count -1 ))
done	
	
}

num_files=$1
test_fs_create_files()
{
total_files=$num_files
echo "We will create $num_files files"

while [ $total_files -gt 0 ]
do
	>$total_files
	ls -al
	((total_files = $total_files - 1))
done	
}

file_length=$1
test_file=test
test_fs_make_file()
{
cd $dest_location
touch $test_file

while [ $file_length -gt 0 ]
do
	echo "$file_length" >> $test_file
	((file_length = $file_length - 1))
done	
}

test_fs_make_file

exit 0
